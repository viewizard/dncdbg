// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "debugger/manageddebugger.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/callbacksqueue.h"
#include "debugger/evalhelpers.h"
#include "debugger/evalstackmachine.h"
#include "debugger/evaluator.h"
#include "debugger/evalwaiter.h"
#include "debugger/frames.h"
#include "debugger/managedcallback.h"
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "debugger/variables.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h"
#include "metadata/modules.h"
#include "metadata/typeprinter.h"
#include "protocol/dapio.h"
#include "utils/waitpid.h"
#include "utils/logger.h"
#include "utils/platform.h"
#include "utils/utf.h"
#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

namespace dncdbg
{

#ifdef FEATURE_PAL

// as alternative, libuuid should be linked...
// the problem is, that in CoreClr > 3.x, in pal/inc/rt/rpc.h,
// MIDL_INTERFACE uses DECLSPEC_UUID, which has empty definition.
extern "C" const IID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

#endif // FEATURE_PAL

namespace
{

constexpr auto startupWaitTimeout = std::chrono::milliseconds(5000);

HRESULT GetSystemEnvironmentAsMap(std::map<std::string, std::string> &outMap)
{
    char *const *const pEnv = GetSystemEnvironment();

    if (pEnv == nullptr)
    {
        return E_FAIL;
    }

    size_t counter = 0;
    while (pEnv[counter] != nullptr)
    {
        const std::string env = pEnv[counter];
        const size_t pos = env.find_first_of('=');
        if (pos != std::string::npos && pos != 0)
        {
            outMap.emplace(env.substr(0, pos), env.substr(pos + 1));
        }

        ++counter;
    }

    return S_OK;
}

// From dbgshim.cpp
bool AreAllHandlesValid(gsl::span<HANDLE> handles)
{
    return std::all_of(handles.begin(), handles.end(), [](HANDLE h)
    {
        return h != INVALID_HANDLE_VALUE; // NOLINT(performance-no-int-to-ptr,cppcoreguidelines-pro-type-cstyle-cast)
    });
}

HRESULT EnumerateCLRs(dbgshim_t &dbgshim, DWORD pid, HANDLE **ppHandleArray, LPWSTR **ppStringArray,
                      DWORD *pdwArrayLength, int tryCount)
{
    int numTries = 0;
    HRESULT hr = S_OK;

    while (numTries < tryCount)
    {
        hr = dbgshim.GetEnumerateCLRs()(pid, ppHandleArray, ppStringArray, pdwArrayLength);

        // From dbgshim.cpp:
        // EnumerateCLRs uses the OS API CreateToolhelp32Snapshot which can return ERROR_BAD_LENGTH or
        // ERROR_PARTIAL_COPY. If we get either of those, we wait 1/10th of a second and try again (that
        // is the recommendation of the OS API owners).
        // In dbgshim the following condition is used:
        //  if ((hr != HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY)) && (hr != HRESULT_FROM_WIN32(ERROR_BAD_LENGTH)))
        // Since we may be attaching to the process which has not loaded coreclr yet, let's give it some time to load.
        if (SUCCEEDED(hr))
        {
            // Just return any other error or if no handles were found (which means the coreclr module wasn't found yet).
            if (*ppHandleArray != nullptr && *pdwArrayLength > 0)
            {

                // If EnumerateCLRs succeeded but any of the handles are INVALID_HANDLE_VALUE, then sleep and retry
                // also. This fixes a race condition where dbgshim catches the coreclr module just being loaded but
                // before g_hContinueStartupEvent has been initialized.
                if (AreAllHandlesValid(gsl::span(*ppHandleArray, *pdwArrayLength)))
                {
                    return hr;
                }
                // Clean up memory allocated in EnumerateCLRs since this path succeeded
                dbgshim.GetCloseCLREnumeration()(*ppHandleArray, *ppStringArray, *pdwArrayLength);

                *ppHandleArray = nullptr;
                *ppStringArray = nullptr;
                *pdwArrayLength = 0;
            }
        }

        // No point in retrying in case of invalid arguments or no such process
        if (hr == E_INVALIDARG || hr == E_FAIL)
        {
            return hr;
        }

        // Sleep and retry enumerating the runtimes
        static constexpr unsigned long sleepTime = 100000UL;
        USleep(sleepTime);
        numTries++;
    }

    // Indicate a timeout
    hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);

    return hr;
}

std::string GetCLRPath(dbgshim_t &dbgshim, DWORD pid, int timeoutSec = 3)
{
    HANDLE *pHandleArray = nullptr;
    LPWSTR *pStringArray = nullptr;
    DWORD dwArrayLength = 0;
    const int tryCount = timeoutSec * 10; // 100ms interval between attempts
    if (FAILED(EnumerateCLRs(dbgshim, pid, &pHandleArray, &pStringArray, &dwArrayLength, tryCount)) ||
        dwArrayLength == 0)
    {
        return {};
    }

    std::string result = to_utf8(*pStringArray);

    dbgshim.GetCloseCLREnumeration()(pHandleArray, pStringArray, dwArrayLength);

    return result;
}

std::string EscapeShellArg(const std::string &arg)
{
    std::string result;
    result.reserve(arg.size() * 2); // Reserve space for worst case (all chars need escaping)

    for (const char c : arg)
    {
        switch (c)
        {
        case '\"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        default:
            result += c;
            break;
        }
    }

    return result;
}

bool IsDirExists(const char *const path)
{
    struct stat info{};

    return stat(path, &info) == 0 &&
           (info.st_mode & S_IFDIR) != 0U;
}

void PrepareSystemEnvironmentArg(const std::map<std::string, std::string> &env, std::vector<char> &outEnv)
{
    // We need to append the environment values while keeping the current process environment block.
    // It works equally for all platforms in coreclr CreateProcessW(), but is not critical for Linux.
    std::map<std::string, std::string> envMap;
    if (SUCCEEDED(GetSystemEnvironmentAsMap(envMap)))
    {
        // Override the system value (PATHs appending needs a complex implementation)
        for (const auto &pair : env)
        {
            auto findEnv = envMap.find(pair.first);
            if (findEnv != envMap.end())
            {
                findEnv->second = pair.second;
            }
            else
            {
                envMap.emplace(pair);
            }
        }
        for (const auto &pair : envMap)
        {
            outEnv.insert(outEnv.end(), pair.first.begin(), pair.first.end());
            outEnv.push_back('=');
            outEnv.insert(outEnv.end(), pair.second.begin(), pair.second.end());
            outEnv.push_back('\0');
        }
        outEnv.push_back('\0');
    }
    else
    {
        for (const auto &pair : env)
        {
            outEnv.insert(outEnv.end(), pair.first.begin(), pair.first.end());
            outEnv.push_back('=');
            outEnv.insert(outEnv.end(), pair.second.begin(), pair.second.end());
            outEnv.push_back('\0');
        }
        outEnv.push_back('\0');
    }
}

HRESULT FindExceptionDispatchInfoThrow(ICorDebugThread *pThread, CORDB_ADDRESS &modAddress, mdMethodDef &methodDef)
{
    HRESULT Status = S_OK;
    static const std::string assemblyName("System.Private.CoreLib.dll");
    static const WSTRING className(W("System.Runtime.ExceptionServices.ExceptionDispatchInfo"));
    static const WSTRING methodName(W("Throw"));
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(EvalHelpers::FindMethodInModule(pThread, assemblyName, className, methodName, &trFunction));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    if (FAILED(Status = trModule->GetBaseAddress(&modAddress)) ||
        FAILED(Status = trFunction->GetToken(&methodDef)))
    {
        modAddress = 0;
        methodDef = mdMethodDefNil;
        return Status;
    }

    return S_OK;
}

} // unnamed namespace

// Caller must care about m_debugProcessRWLock.
HRESULT ManagedDebugger::CheckDebugProcess()
{
    if (m_trProcess == nullptr)
    {
        return E_FAIL;
    }

    // We might have a case when the process has exited/detached, but m_trProcess is still not freed and holds an invalid object.
    // Note, we can't hold this lock, since this could deadlock execution at ICorDebugManagedCallback::ExitProcess call.
    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (m_processAttachedState == ProcessAttachedState::Unattached)
    {
        return E_FAIL;
    }
    lockAttachedMutex.unlock();

    return S_OK;
}

bool ManagedDebugger::HaveDebugProcess()
{
    const ReadLock r_lock(m_debugProcessRWLock);
    return SUCCEEDED(CheckDebugProcess());
}

void ManagedDebugger::NotifyProcessCreated()
{
    std::unique_lock<std::mutex> lock(m_processAttachedMutex);
    m_processAttachedState = ProcessAttachedState::Attached;
    lock.unlock();
    m_processAttachedCV.notify_one();
}

void ManagedDebugger::NotifyProcessExited()
{
    std::unique_lock<std::mutex> lock(m_processAttachedMutex);
    m_processAttachedState = ProcessAttachedState::Unattached;
    lock.unlock();
    m_processAttachedCV.notify_all();
}

// Caller must care about m_debugProcessRWLock.
void ManagedDebugger::DisableAllBreakpointsAndSteppers()
{
    m_uniqueSteppers->DisableAllSteppers(m_trProcess); // Async stepper could have breakpoints active, disable them first.
    m_sharedBreakpoints->DeleteAll();
    dncdbg::Breakpoints::DisableAll(m_trProcess); // Last one, disable all breakpoints on all domains, even if we don't hold them.
}

void ManagedDebugger::SetLastStoppedThread(ICorDebugThread *pThread)
{
    SetLastStoppedThreadId(getThreadId(pThread));
}

void ManagedDebugger::SetLastStoppedThreadId(ThreadId threadId)
{
    const std::scoped_lock<std::mutex> lock(m_lastStoppedMutex);
    m_lastStoppedThreadId = threadId;

    const ReadLock r_lock(m_debugProcessRWLock);

    m_sharedBreakpoints->SetLastStoppedIlOffset(m_trProcess, m_lastStoppedThreadId);
}

void ManagedDebugger::InvalidateLastStoppedThreadId()
{
    SetLastStoppedThreadId(ThreadId::AllThreads);
}

ThreadId ManagedDebugger::GetLastStoppedThreadId()
{
    const std::scoped_lock<std::mutex> lock(m_lastStoppedMutex);
    return m_lastStoppedThreadId;
}

ManagedDebugger::ManagedDebugger()
    : m_lastStoppedThreadId(ThreadId::AllThreads),
      m_sharedThreads(new Threads),
      m_sharedDebugInfo(new DebugInfo),
      m_sharedModules(new Modules),
      m_sharedEvalWaiter(new EvalWaiter),
      m_sharedEvalHelpers(new EvalHelpers(m_sharedEvalWaiter)),
      m_sharedEvalStackMachine(new EvalStackMachine),
      m_sharedEvaluator(new Evaluator(m_sharedDebugInfo, m_sharedEvalHelpers, m_sharedEvalStackMachine)),
      m_sharedVariables(new Variables(m_sharedEvalHelpers, m_sharedEvaluator, m_sharedEvalStackMachine)),
      m_uniqueSteppers(new Steppers(m_sharedDebugInfo, m_sharedEvalHelpers)),
      m_sharedBreakpoints(new Breakpoints(m_sharedDebugInfo, m_sharedEvaluator, m_sharedVariables)),
      m_sharedCallbacksQueue(nullptr),
      m_uniqueManagedCallback(nullptr),
      m_ioredirect([](IORedirect::StreamType type, gsl::span<char> text)
            {
                InputCallback(type, text);
            })
{
    m_sharedEvalStackMachine->SetupEval(m_sharedEvaluator, m_sharedEvalHelpers, m_sharedEvalWaiter);
}

ManagedDebugger::~ManagedDebugger()
{
    m_sharedEvalStackMachine->ResetEval();
}

HRESULT ManagedDebugger::Initialize()
{
    // TODO: Report capabilities and check client support
    m_startMethod = StartMethod::None;
    return S_OK;
}

HRESULT ManagedDebugger::RunIfReady()
{
    FrameId::invalidate();

    if (m_startMethod == StartMethod::None || !m_isConfigurationDone)
    {
        return S_OK;
    }

    switch (m_startMethod)
    {
    case StartMethod::Launch:
        return RunProcess(m_execPath, m_execArgs);
    case StartMethod::Attach:
        return AttachToProcess();
    default:
        return E_FAIL;
    }

    // Unreachable
    return E_FAIL;
}

HRESULT ManagedDebugger::Attach(int pid)
{
    m_startMethod = StartMethod::Attach;
    m_processId = pid;
    return RunIfReady();
}

HRESULT ManagedDebugger::Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
                                const std::map<std::string, std::string> &env, const std::string &cwd, bool stopAtEntry)
{
    m_startMethod = StartMethod::Launch;
    m_execPath = fileExec;
    m_execArgs = execArgs;
    m_cwd = cwd;
    m_env = env;
    m_sharedBreakpoints->SetStopAtEntry(stopAtEntry);
    return RunIfReady();
}

HRESULT ManagedDebugger::ConfigurationDone()
{
    m_isConfigurationDone = true;

    return RunIfReady();
}

HRESULT ManagedDebugger::Disconnect(DisconnectAction action)
{
    bool terminate = false;
    switch (action)
    {
    case DisconnectAction::Default:
        switch (m_startMethod)
        {
        case StartMethod::Launch:
            terminate = true;
            break;
        case StartMethod::Attach:
            terminate = false;
            break;
        default:
            return E_FAIL;
        }
        break;
    case DisconnectAction::Terminate:
        terminate = true;
        break;
    case DisconnectAction::Detach:
        if (m_startMethod != StartMethod::Attach)
        {
            LOGE(log << "Can't detach debugger from child process.\n");
            return E_INVALIDARG;
        }
        terminate = false;
        break;
    default:
        return E_FAIL;
    }

    if (!terminate)
    {
        const HRESULT Status = DetachFromProcess();
        if (SUCCEEDED(Status))
        {
            DAPIO::EmitTerminatedEvent();
        }

        return Status;
    }

    return TerminateProcess();
}

HRESULT ManagedDebugger::StepCommand(ThreadId threadId, StepType stepType)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (m_sharedEvalWaiter->IsEvalRunning())
    {
        // Important! Abort all evals before 'Step' in protocol, during eval we have inconsistent thread state.
        LOGE(log << "Can't 'Step' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (m_sharedCallbacksQueue->IsRunning())
    {
        LOGW(log << "Can't 'Step', process already running.");
        return E_FAIL;
    }

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(m_trProcess->GetThread(static_cast<int>(threadId), &trThread));
    IfFailRet(m_uniqueSteppers->SetupStep(trThread, stepType));

    m_sharedVariables->Cleanup();
    FrameId::invalidate();               // Clear all created during break frames.
    DAPIO::EmitContinuedEvent(threadId); // DAP needs thread ID.

    // Note, process continue must be after event emitted, since we could get new stop event from queue here.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_trProcess)))
    {
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }

    return Status;
}

HRESULT ManagedDebugger::Continue(ThreadId threadId)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (m_sharedEvalWaiter->IsEvalRunning())
    {
        // Important! Abort all evals before 'Continue' in protocol, during eval we have inconsistent thread state.
        LOGE(log << "Can't 'Continue' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (m_sharedCallbacksQueue->IsRunning())
    {
        LOGI(log << "Can't 'Continue', process already running.");
        return S_OK; // Send 'OK' response, but don't generate continue event.
    }

    m_sharedVariables->Cleanup();
    FrameId::invalidate();               // Clear all created during break frames.
    DAPIO::EmitContinuedEvent(threadId); // DAP needs thread ID.

    // Note, process continue must be after event emitted, since we could get new stop event from queue here.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_trProcess)))
    {
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }

    return Status;
}

HRESULT ManagedDebugger::Pause(ThreadId lastStoppedThread)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedCallbacksQueue->Pause(m_trProcess, lastStoppedThread);
}

HRESULT ManagedDebugger::GetThreads(std::vector<Thread> &threads)
{
    return m_sharedThreads->GetThreads(threads);
}

void ManagedDebugger::StartupCallback(IUnknown *pCordb, void *parameter, HRESULT hr)
{
    auto *self = static_cast<ManagedDebugger *>(parameter);

    if (FAILED(hr))
    {
        std::ostringstream ss;
        ss << "Error: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << hr;
        if (CORDBG_E_DEBUG_COMPONENT_MISSING == hr)
        {
            ss << " component that is necessary for CLR debugging cannot be located.";
        }
        else if (CORDBG_E_INCOMPATIBLE_PROTOCOL == hr)
        {
            ss << " mscordbi or mscordaccore libs are not the same version as the target CoreCLR.";
        }
        DAPIO::EmitOutputEvent({OutputCategory::StdErr, ss.str()});
        self->StartupCallbackHR = hr;
        return;
    }

    self->Startup(pCordb);

    if (self->m_unregisterToken != nullptr)
    {
        self->m_dbgshim.GetUnregisterForRuntimeStartup()(self->m_unregisterToken);
        self->m_unregisterToken = nullptr;
    }
}

HRESULT ManagedDebugger::Startup(IUnknown *punk)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebug> trDebug;
    IfFailRet(punk->QueryInterface(IID_ICorDebug, reinterpret_cast<void **>(&trDebug)));

    IfFailRet(trDebug->Initialize());

    if (m_clrPath.empty())
    {
        m_clrPath = GetCLRPath(m_dbgshim, m_processId);
    }

    m_sharedCallbacksQueue = std::make_shared<CallbacksQueue>(*this);
    m_uniqueManagedCallback = std::make_unique<ManagedCallback>(*this, m_sharedCallbacksQueue);
    if (FAILED(Status = trDebug->SetManagedHandler(m_uniqueManagedCallback.get())))
    {
        trDebug->Terminate();
        m_uniqueManagedCallback.reset();
        m_sharedCallbacksQueue.reset();
        return Status;
    }

    ToRelease<ICorDebugProcess> trProcess;
    if (FAILED(Status = trDebug->DebugActiveProcess(m_processId, FALSE, &trProcess)))
    {
        trDebug->Terminate();
        m_uniqueManagedCallback.reset();
        m_sharedCallbacksQueue.reset();
        return Status;
    }

    WriteLock w_lock(m_debugProcessRWLock);

    m_trProcess = trProcess.Detach();
    m_trDebug = trDebug.Detach();

    w_lock.unlock();

#ifdef FEATURE_PAL
    WaitpidHook::SetupTrackingPID(static_cast<pid_t>(m_processId));
#endif // FEATURE_PAL

    return S_OK;
}

HRESULT ManagedDebugger::RunProcess(const std::string &fileExec, const std::vector<std::string> &execArgs)
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    std::ostringstream ss;
    ss << "\"" << fileExec << "\"";
    for (const std::string &arg : execArgs)
    {
        ss << " \"" << EscapeShellArg(arg) << "\"";
    }

    m_clrPath.clear();

    HANDLE resumeHandle = nullptr; // Fake thread handle for the process resume

    std::vector<char> outEnv;
    PrepareSystemEnvironmentArg(m_env, outEnv);

    // cwd in launch.json set working directory for debugger https://code.visualstudio.com/docs/python/debugging#_cwd
    if (!m_cwd.empty() &&
        (!IsDirExists(m_cwd.c_str()) || !SetWorkDir(m_cwd)))
    {
        m_cwd.clear();
    }

    m_ioredirect.exec([&]() {
            Status = m_dbgshim.GetCreateProcessForLaunch()(
                const_cast<WCHAR*>(to_utf16(ss.str()).c_str()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                TRUE, // Suspend process
                outEnv.empty() ? nullptr : outEnv.data(),
                m_cwd.empty() ? nullptr : reinterpret_cast<const WCHAR *>(to_utf16(m_cwd).c_str()),
                &m_processId, &resumeHandle);
        });

    if (FAILED(Status))
    {
        return Status;
    }

#ifdef FEATURE_PAL
    WaitpidHook::SetupTrackingPID(static_cast<pid_t>(m_processId));
#endif // FEATURE_PAL

    IfFailRet(m_dbgshim.GetRegisterForRuntimeStartup()(m_processId, ManagedDebugger::StartupCallback, this, &m_unregisterToken));

    // Resume the process so that StartupCallback can run
    IfFailRet(m_dbgshim.GetResumeProcess()(resumeHandle));
    m_dbgshim.GetCloseResumeHandle()(resumeHandle);

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (!m_processAttachedCV.wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [this] { return m_processAttachedState == ProcessAttachedState::Attached; }))
    {
        IfFailRet(StartupCallbackHR);
        return E_FAIL;
    }

    DAPIO::EmitProcessEvent(m_processId, fileExec, m_startMethod);

    return S_OK;
}

HRESULT ManagedDebugger::CheckNoProcess()
{
    const ReadLock r_lock(m_debugProcessRWLock);

    if (m_trProcess == nullptr)
    {
        return S_OK;
    }

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (m_processAttachedState == ProcessAttachedState::Attached)
    {
        return E_FAIL; // Already attached
    }
    lockAttachedMutex.unlock();

    Cleanup();
    return S_OK;
}

HRESULT ManagedDebugger::DetachFromProcess()
{
    do
    {
        const ReadLock r_lock(m_debugProcessRWLock);
        const std::scoped_lock<std::mutex> guardAttachedMutex(m_processAttachedMutex);
        if (m_processAttachedState == ProcessAttachedState::Unattached)
        {
            break;
        }

        if (m_trProcess == nullptr)
        {
            return E_FAIL;
        }

        BOOL procRunning = FALSE;
        if (SUCCEEDED(m_trProcess->IsRunning(&procRunning)) && procRunning == TRUE)
        {
            m_trProcess->Stop(0);
        }

        DisableAllBreakpointsAndSteppers();

        HRESULT Status = S_OK;
        if (FAILED(Status = m_trProcess->Detach()))
        {
            LOGE(log << "Process detach failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        }

        m_processAttachedState = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    } while (false);

    Cleanup();
    return S_OK;
}

HRESULT ManagedDebugger::TerminateProcess()
{
    do
    {
        const ReadLock r_lock(m_debugProcessRWLock);
        std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
        if (m_processAttachedState == ProcessAttachedState::Unattached)
        {
            break;
        }

        if (m_trProcess == nullptr)
        {
            return E_FAIL;
        }

        BOOL procRunning = FALSE;
        if (SUCCEEDED(m_trProcess->IsRunning(&procRunning)) && procRunning == TRUE)
        {
            m_trProcess->Stop(0);
        }

        DisableAllBreakpointsAndSteppers();

        HRESULT Status = S_OK;
        if (SUCCEEDED(Status = m_trProcess->Terminate(0)))
        {
            m_processAttachedCV.wait(lockAttachedMutex, [this] { return m_processAttachedState == ProcessAttachedState::Unattached; });
            break;
        }

        LOGE(log << "Process terminate failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        m_processAttachedState = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    } while (false);

    Cleanup();
    return S_OK;
}

void ManagedDebugger::Cleanup()
{
    m_sharedDebugInfo->Cleanup();
    m_sharedEvalHelpers->Cleanup();
    m_sharedVariables->Cleanup();

    const WriteLock w_lock(m_debugProcessRWLock);

    assert((m_trProcess && m_trDebug && m_uniqueManagedCallback && m_sharedCallbacksQueue) ||
           (!m_trProcess && !m_trDebug && !m_uniqueManagedCallback && !m_sharedCallbacksQueue));

    if (m_trProcess == nullptr)
    {
        return;
    }

    m_trProcess.Free();

    m_trDebug->Terminate();
    m_trDebug.Free();

    if (m_uniqueManagedCallback->GetRefCount() > 0)
    {
        LOGW(log << "ManagedCallback was not properly released by ICorDebug");
    }
    m_uniqueManagedCallback.reset(nullptr);
    m_sharedCallbacksQueue = nullptr;
}

HRESULT ManagedDebugger::AttachToProcess()
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    m_clrPath = GetCLRPath(m_dbgshim, m_processId);
    if (m_clrPath.empty())
    {
        return E_INVALIDARG; // Unable to find libcoreclr.so
    }

    static constexpr uint32_t bufSize = 100;
    std::array<WCHAR, bufSize> pBuffer{};
    DWORD dwLength = 0;
    IfFailRet(m_dbgshim.GetCreateVersionStringFromModule()(
        m_processId, reinterpret_cast<const WCHAR *>(to_utf16(m_clrPath).c_str()), pBuffer.data(), bufSize, &dwLength));

    ToRelease<IUnknown> trCordb;
    IfFailRet(m_dbgshim.GetCreateDebuggingInterfaceFromVersionEx()(CorDebugVersion_4_0, pBuffer.data(), &trCordb));

    m_unregisterToken = nullptr;
    IfFailRet(Startup(trCordb));

    DAPIO::EmitProcessEvent(m_processId, "dotnet", m_startMethod);

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (!m_processAttachedCV.wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [this] { return m_processAttachedState == ProcessAttachedState::Attached; }))
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT ManagedDebugger::GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(m_trProcess->GetThread(static_cast<int>(threadId), &trThread));
    return m_sharedBreakpoints->GetExceptionInfo(trThread, exceptionInfo);
}

HRESULT ManagedDebugger::SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                                 std::vector<Breakpoint> &breakpoints)
{
    return m_sharedBreakpoints->SetExceptionBreakpoints(exceptionBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::SetSourceBreakpoints(const std::string &filename,
                                              const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                              std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return m_sharedBreakpoints->SetSourceBreakpoints(haveProcess, filename, sourceBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                                std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return m_sharedBreakpoints->SetFunctionBreakpoints(haveProcess, functionBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::GetFrameLocation(ICorDebugFrame *pFrame, ThreadId threadId, FrameLevel level,
                                          StackFrame &stackFrame)
{
    HRESULT Status = S_OK;

    std::string methodName;
    if (FAILED(TypePrinter::GetMethodName(pFrame, methodName)))
    {
        methodName = "Unnamed method in optimized code";
    }
    stackFrame = StackFrame(threadId, level, methodName);

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(pFrame->GetFunction(&trFunc));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));

    uint32_t ilOffset = 0;
    SequencePoint sp;
    if (SUCCEEDED(m_sharedDebugInfo->GetFrameILAndSequencePoint(pFrame, ilOffset, sp)))
    {
        stackFrame.source = Source(sp.document);
        stackFrame.line = sp.startLine;
        stackFrame.column = sp.startColumn;
        stackFrame.endLine = sp.endLine;
        stackFrame.endColumn = sp.endColumn;
    }

    IfFailRet(Modules::GetModuleId(trModule, stackFrame.moduleId));

    return S_OK;
}

bool ManagedDebugger::IsTopFrameExceptionDispatchInfoThrow(ICorDebugThread *pThread)
{
    if ((PrivateCoreLibModAddress == 0 || ExceptionDispatchInfoThrowMethodDef == mdMethodDefNil) &&
        FAILED(FindExceptionDispatchInfoThrow(pThread, PrivateCoreLibModAddress, ExceptionDispatchInfoThrowMethodDef)))
    {
        return false;
    }

    ToRelease<ICorDebugFrame> trFrame;
    ToRelease<ICorDebugFunction> trFunction;
    ToRelease<ICorDebugModule> trModule;
    CORDB_ADDRESS modAddress = 0;
    mdMethodDef methodDef = mdMethodDefNil;

    return SUCCEEDED(pThread->GetActiveFrame(&trFrame)) &&
           trFrame != nullptr &&
           SUCCEEDED(trFrame->GetFunction(&trFunction)) &&
           SUCCEEDED(trFunction->GetModule(&trModule)) &&
           SUCCEEDED(trModule->GetBaseAddress(&modAddress)) &&
           SUCCEEDED(trFunction->GetToken(&methodDef)) &&
           PrivateCoreLibModAddress == modAddress &&
           ExceptionDispatchInfoThrowMethodDef == methodDef;
}

HRESULT ManagedDebugger::GetExceptionStackTrace(ICorDebugThread *pThread, std::string &stackTrace)
{
    ToRelease<ICorDebugValue> trExceptionValue;
    if (FAILED(pThread->GetCurrentException(&trExceptionValue)) ||
        trExceptionValue == nullptr)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    IfFailRet(m_sharedEvaluator->WalkMembers(
        trExceptionValue, pThread, FrameLevel{0}, nullptr, false,
        [&](ICorDebugType *, bool, const std::string &memberName,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *) -> HRESULT
        {
            if (memberName != "StackTrace")
            {
                return S_OK;
            }

            HRESULT Status = S_OK;
            ToRelease<ICorDebugValue> trResultValue;
            IfFailRet(getValue(&trResultValue, true));

            BOOL isNull = TRUE;
            ToRelease<ICorDebugReferenceValue> trReferenceValue;
            if (SUCCEEDED(trResultValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue))) &&
                SUCCEEDED(trReferenceValue->IsNull(&isNull)) && isNull == FALSE)
            {
                PrintValue(trResultValue, stackTrace, false);
            }

            return S_CAN_EXIT; // Fast exit from loop.
        }));

    return stackTrace.empty() ? E_FAIL : S_OK;
}

HRESULT ManagedDebugger::GetManagedStackTrace(ICorDebugThread *pThread, ThreadId threadId, FrameLevel startFrame,
                                              unsigned maxFrames, std::vector<StackFrame> &stackFrames)
{
    HRESULT Status = S_OK;
    int currentFrame = -1;

    // CoreCLR native frame + at least one user's native frame
    static const std::string FrameCLRNativeText = "[Native Frames]";
    // This frame usually indicate some fail during managed unwind
    static const std::string FrameUnknownText = "[Unknown Frame]";
    // Non-user code related frame when Just My Code is enabled.
    static const std::string ExternalCodeText = "[External Code]";

    // Exception rethrown with System.Runtime.ExceptionServices.ExceptionDispatchInfo.Throw() case.
    // Note, this is optional part, ignore errors.
    if (IsTopFrameExceptionDispatchInfoThrow(pThread))
    {
        // In case of async method, user code could be moved to `.NET TP Worker` thread and in case of
        // unhandled exception in this user code, exception will be caught and rethrown by non-user code
        // in the initial async method code execution thread.
        static constexpr int triesLimit = 3;
        for (int tryCount = 0; tryCount < triesLimit; tryCount++)
        {
            std::string stackTrace;
            if (SUCCEEDED(GetExceptionStackTrace(pThread, stackTrace)))
            {
                std::stringstream ss(stackTrace);
                // The stackTrace strings from exception usually looks like:
                // at Program.Func2(string[] strvect) in /home/user/work/vscode_test/utils.cs:line 122
                // at Program.Func1<int, char>() in /home/user/work/vscode_test/utils.cs:line 78
                // at Program.Main() in /home/user/work/vscode_test/Program.cs:line 25
                // at Program.Main() in C:\Users/localuser/work/vscode_test\Program.cs:line 25
                while (!ss.eof())
                {
                    std::string line;
                    std::getline(ss, line, '\n');
                    const size_t lastcolon = line.find_last_of(':');
                    if (lastcolon == std::string::npos)
                    {
                        continue;
                    }

                    size_t beginpath = line.find_first_of('/');
                    if (beginpath == std::string::npos)
                    {
                        beginpath = line.find_first_of('\\');
                        if (beginpath == std::string::npos)
                        {
                            continue;
                        }
                    }

                    // append disk name, if exists
                    beginpath = line.find_last_of(' ', beginpath);
                    if (beginpath == std::string::npos)
                    {
                        continue;
                    }

                    beginpath++;
                    if (beginpath >= lastcolon)
                    {
                        continue;
                    }

                    // remove leading spaces and the first word ("at" for the case of English locale)
                    size_t beginname = line.find_first_not_of(' ');
                    if (beginname == std::string::npos)
                    {
                        continue;
                    }

                    beginname = line.find_first_of(' ', beginname);
                    if (beginname == std::string::npos)
                    {
                        continue;
                    }
                    beginname++;

                    // the function name ends with the last ')' before the beginning of fullpath
                    size_t endname = line.find_last_of(')', beginpath);
                    if (endname == std::string::npos)
                    {
                        continue;
                    }
                    endname++;

                    if (beginname >= endname)
                    {
                        continue;
                    }

                    // look for the line number after the last colon
                    const size_t beginlinenum = line.find_first_of("0123456789", lastcolon);
                    const size_t endlinenum = line.find_first_not_of("0123456789", beginlinenum);
                    if (beginlinenum == std::string::npos)
                    {
                        continue;
                    }

                    currentFrame++;
                    if (currentFrame < static_cast<int>(startFrame))
                    {
                        continue;
                    }
                    if (maxFrames != 0 && currentFrame >= static_cast<int>(startFrame) + static_cast<int>(maxFrames))
                    {
                        break;
                    }

                    const int lineNum(std::stoi(line.substr(beginlinenum, endlinenum)));
                    stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, line.substr(beginname, endname - beginname));
                    stackFrames.back().source = Source(line.substr(beginpath, lastcolon - beginpath));
                    stackFrames.back().line = lineNum;
                    stackFrames.back().endLine = lineNum;
                }
                break;
            }
        }
    }

    bool prevFrameExternal = false;

    IfFailRet(WalkFrames(pThread,
        [&](FrameType frameType, ICorDebugFrame *pFrame) -> HRESULT
        {
            if (IsJustMyCode())
            {
                ToRelease<ICorDebugFunction> trFunction;
                ToRelease<ICorDebugFunction2> trFunction2;
                BOOL JMCStatus = FALSE;
                if (frameType == FrameType::CLRManaged &&
                    SUCCEEDED(pFrame->GetFunction(&trFunction)) &&
                    SUCCEEDED(trFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&trFunction2))) &&
                    SUCCEEDED(trFunction2->GetJMCStatus(&JMCStatus)) &&
                    JMCStatus == TRUE)
                {
                    prevFrameExternal = false;
                }
                else
                {
                    if (!prevFrameExternal)
                    {
                        currentFrame++;
                        stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, ExternalCodeText);
                        prevFrameExternal = true;
                    }
                    return S_OK; // Continue walk.
                }
            }

            currentFrame++;

            if (currentFrame < static_cast<int>(startFrame))
            {
                return S_OK; // Continue walk.
            }
            if (maxFrames != 0 && currentFrame >= static_cast<int>(startFrame) + static_cast<int>(maxFrames))
            {
                return S_CAN_EXIT; // Fast exit from loop.
            }

            switch (frameType)
            {
            case FrameType::Unknown:
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, FrameUnknownText);
                break;
            case FrameType::CLRNative:
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, FrameCLRNativeText);
                break;
            case FrameType::CLRInternal:
            {
                ToRelease<ICorDebugInternalFrame> trInternalFrame;
                CorDebugInternalFrameType corFrameType = STUBFRAME_NONE;
                if (SUCCEEDED(pFrame->QueryInterface(IID_ICorDebugInternalFrame, reinterpret_cast<void **>(&trInternalFrame))))
                {
                    trInternalFrame->GetFrameType(&corFrameType);
                }
                std::string name = "[";
                name += GetInternalTypeName(corFrameType);
                name += "]";
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, name);
                break;
            }
            case FrameType::CLRManaged:
            {
                StackFrame stackFrame;
                GetFrameLocation(pFrame, threadId, FrameLevel{currentFrame}, stackFrame);
                stackFrames.push_back(stackFrame);
                break;
            }
            }

            return S_OK; // Continue walk.
        }));

    return S_OK;
}

HRESULT ManagedDebugger::GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                                       std::vector<StackFrame> &stackFrames)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> trThread;
    if (SUCCEEDED(Status = m_trProcess->GetThread(static_cast<int>(threadId), &trThread)))
    {
        return GetManagedStackTrace(trThread, threadId, startFrame, maxFrames, stackFrames);
    }

    return Status;
}

HRESULT ManagedDebugger::GetVariables(uint32_t variablesReference, VariablesFilter filter, int start, int count,
                                      std::vector<Variable> &variables)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->GetVariables(m_trProcess, variablesReference, filter, start, count, variables);
}

HRESULT ManagedDebugger::GetScopes(FrameId frameId, std::vector<Scope> &scopes)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->GetScopes(m_trProcess, frameId, scopes);
}

HRESULT ManagedDebugger::Evaluate(FrameId frameId, const std::string &expression, Variable &variable,
                                  std::string &output)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->Evaluate(m_trProcess, frameId, expression, variable, output);
}

void ManagedDebugger::CancelEvalRunning()
{
    m_sharedEvalWaiter->CancelEvalRunning();
}

HRESULT ManagedDebugger::SetVariable(const std::string &name, const std::string &value, uint32_t ref,
                                     std::string &output)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->SetVariable(m_trProcess, name, value, ref, output);
}

HRESULT ManagedDebugger::SetExpression(FrameId frameId, const std::string &expression,
                                       const std::string &value, std::string &output)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->SetExpression(m_trProcess, frameId, expression, value, output);
}

void ManagedDebugger::SetJustMyCode(bool enable)
{
    m_justMyCode = enable;
    m_uniqueSteppers->SetJustMyCode(enable);
    m_sharedBreakpoints->SetJustMyCode(enable);
}

void ManagedDebugger::SetStepFiltering(bool enable)
{
    m_stepFiltering = enable;
    m_uniqueSteppers->SetStepFiltering(enable);
}

void ManagedDebugger::SetEvalFlags(uint32_t evalFlags)
{
    m_sharedEvalHelpers->SetEvalFlags(evalFlags);
}

void ManagedDebugger::InputCallback(IORedirect::StreamType type, gsl::span<char> text)
{
    DAPIO::EmitOutputEvent(OutputEvent(type == IORedirect::StreamType::Stderr ? OutputCategory::StdErr : OutputCategory::StdOut, {text.data(), text.size()}));
}

void ManagedDebugger::GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules)
{
    m_sharedModules->GetModules(startModule, moduleCount, modules, totalModules);
}

} // namespace dncdbg
