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
#include "metadata/modules.h"
#include "metadata/typeprinter.h"
#include "protocol/dap.h"
#include "utils/waitpid.h"
#include "utils/logger.h"
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
const auto startupWaitTimeout = std::chrono::milliseconds(5000);

int GetSystemEnvironmentAsMap(std::map<std::string, std::string> &outMap)
{
    char *const *const pEnv = GetSystemEnvironment();

    if (pEnv == nullptr)
        return -1;

    int counter = 0;
    while (pEnv[counter] != nullptr)
    {
        const std::string env = pEnv[counter];
        const size_t pos = env.find_first_of('=');
        if (pos != std::string::npos && pos != 0)
            outMap.emplace(env.substr(0, pos), env.substr(pos + 1));

        ++counter;
    }

    return 0;
}
} // namespace

// Caller must care about m_debugProcessRWLock.
HRESULT ManagedDebugger::CheckDebugProcess()
{
    if (m_iCorProcess == nullptr)
        return E_FAIL;

    // We might have case, when process was exited/detached, but m_iCorProcess still not free and hold invalid object.
    // Note, we can't hold this lock, since this could deadlock execution at ICorDebugManagedCallback::ExitProcess call.
    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (m_processAttachedState == ProcessAttachedState::Unattached)
        return E_FAIL;
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
    m_uniqueSteppers->DisableAllSteppers(m_iCorProcess); // Async stepper could have breakpoints active, disable them first.
    m_uniqueBreakpoints->DeleteAll();
    dncdbg::Breakpoints::DisableAll(m_iCorProcess); // Last one, disable all breakpoints on all domains, even if we don't hold them.
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

    m_uniqueBreakpoints->SetLastStoppedIlOffset(m_iCorProcess, m_lastStoppedThreadId);
}

void ManagedDebugger::InvalidateLastStoppedThreadId()
{
    SetLastStoppedThreadId(ThreadId::AllThreads);
}

ThreadId ManagedDebugger::GetLastStoppedThreadId()
{
    LogFuncEntry();

    const std::scoped_lock<std::mutex> lock(m_lastStoppedMutex);
    return m_lastStoppedThreadId;
}

ManagedDebugger::ManagedDebugger(DAP *pProtocol_)
    : m_processAttachedState(ProcessAttachedState::Unattached),
      m_lastStoppedThreadId(ThreadId::AllThreads),
      m_startMethod(StartMethod::None),
      m_isConfigurationDone(false),
      pProtocol(pProtocol_),
      m_sharedThreads(new Threads),
      m_sharedModules(new Modules),
      m_sharedEvalWaiter(new EvalWaiter),
      m_sharedEvalHelpers(new EvalHelpers(m_sharedModules, m_sharedEvalWaiter)),
      m_sharedEvalStackMachine(new EvalStackMachine),
      m_sharedEvaluator(new Evaluator(m_sharedModules, m_sharedEvalHelpers, m_sharedEvalStackMachine)),
      m_sharedVariables(new Variables(m_sharedEvalHelpers, m_sharedEvaluator, m_sharedEvalStackMachine)),
      m_uniqueSteppers(new Steppers(m_sharedModules, m_sharedEvalHelpers)),
      m_uniqueBreakpoints(new Breakpoints(m_sharedModules, m_sharedEvaluator, m_sharedVariables)),
      m_sharedCallbacksQueue(nullptr),
      m_uniqueManagedCallback(nullptr),
      m_justMyCode(true),
      m_stepFiltering(true),
      m_unregisterToken(nullptr),
      m_processId(0),
      m_ioredirect({IOSystem::unnamed_pipe(), IOSystem::unnamed_pipe(), IOSystem::unnamed_pipe()},
            [this](auto &&PH1, auto &&PH2)
            {
                InputCallback(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
            }),
      StartupCallbackHR(S_OK)
{
    m_sharedEvalStackMachine->SetupEval(m_sharedEvaluator, m_sharedEvalHelpers, m_sharedEvalWaiter);
}

ManagedDebugger::~ManagedDebugger()
{
    m_sharedThreads->ResetEvaluator();
    m_sharedEvalStackMachine->ResetEval();
}

HRESULT ManagedDebugger::Initialize()
{
    LogFuncEntry();

    // TODO: Report capabilities and check client support
    m_startMethod = StartMethod::None;
    return S_OK;
}

HRESULT ManagedDebugger::RunIfReady()
{
    FrameId::invalidate();

    if (m_startMethod == StartMethod::None || !m_isConfigurationDone)
        return S_OK;

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
    LogFuncEntry();

    m_startMethod = StartMethod::Attach;
    m_processId = pid;
    return RunIfReady();
}

HRESULT ManagedDebugger::Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
                                const std::map<std::string, std::string> &env, const std::string &cwd, bool stopAtEntry)
{
    LogFuncEntry();

    m_startMethod = StartMethod::Launch;
    m_execPath = fileExec;
    m_execArgs = execArgs;
    m_cwd = cwd;
    m_env = env;
    m_uniqueBreakpoints->SetStopAtEntry(stopAtEntry);
    return RunIfReady();
}

HRESULT ManagedDebugger::ConfigurationDone()
{
    LogFuncEntry();

    m_isConfigurationDone = true;

    return RunIfReady();
}

HRESULT ManagedDebugger::Disconnect(DisconnectAction action)
{
    LogFuncEntry();

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
            LOGE("Can't detach debugger form child process.\n");
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
            pProtocol->EmitTerminatedEvent();

        return Status;
    }

    return TerminateProcess();
}

HRESULT ManagedDebugger::StepCommand(ThreadId threadId, StepType stepType)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (m_sharedEvalWaiter->IsEvalRunning())
    {
        // Important! Abort all evals before 'Step' in protocol, during eval we have inconsistent thread state.
        LOGE("Can't 'Step' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (m_sharedCallbacksQueue->IsRunning())
    {
        LOGW("Can't 'Step', process already running.");
        return E_FAIL;
    }

    ToRelease<ICorDebugThread> pThread;
    IfFailRet(m_iCorProcess->GetThread(static_cast<int>(threadId), &pThread));
    IfFailRet(m_uniqueSteppers->SetupStep(pThread, stepType));

    m_sharedVariables->Clear();
    FrameId::invalidate();                   // Clear all created during break frames.
    pProtocol->EmitContinuedEvent(threadId); // DAP need thread ID.

    // Note, process continue must be after event emitted, since we could get new stop event from queue here.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_iCorProcess)))
        LOGE("Continue failed: 0x%08x", Status);

    return Status;
}

HRESULT ManagedDebugger::Continue(ThreadId threadId)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (m_sharedEvalWaiter->IsEvalRunning())
    {
        // Important! Abort all evals before 'Continue' in protocol, during eval we have inconsistent thread state.
        LOGE("Can't 'Continue' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (m_sharedCallbacksQueue->IsRunning())
    {
        LOGI("Can't 'Continue', process already running.");
        return S_OK; // Send 'OK' response, but don't generate continue event.
    }

    m_sharedVariables->Clear();
    FrameId::invalidate();                   // Clear all created during break frames.
    pProtocol->EmitContinuedEvent(threadId); // DAP need thread ID.

    // Note, process continue must be after event emitted, since we could get new stop event from queue here.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_iCorProcess)))
        LOGE("Continue failed: 0x%08x", Status);

    return Status;
}

HRESULT ManagedDebugger::Pause(ThreadId lastStoppedThread)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedCallbacksQueue->Pause(m_iCorProcess, lastStoppedThread);
}

HRESULT ManagedDebugger::GetThreads(std::vector<Thread> &threads)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedThreads->GetThreadsWithState(m_iCorProcess, threads);
}

void ManagedDebugger::StartupCallback(IUnknown *pCordb, void *parameter, HRESULT hr)
{
    auto *self = static_cast<ManagedDebugger *>(parameter);

    if (FAILED(hr))
    {
        std::ostringstream ss;
        ss << "Error: 0x" << std::setw(8) << std::setfill('0') << std::hex << hr;
        if (CORDBG_E_DEBUG_COMPONENT_MISSING == hr)
        {
            ss << " component that is necessary for CLR debugging cannot be located.";
        }
        else if (CORDBG_E_INCOMPATIBLE_PROTOCOL == hr)
        {
            ss << " mscordbi or mscordaccore libs is not the same version as the target CoreCLR.";
        }
        self->pProtocol->EmitOutputEvent({OutputCategory::StdErr, ss.str()});
        self->StartupCallbackHR = hr;
        return;
    }

    self->Startup(pCordb);

    if (self->m_unregisterToken != nullptr)
    {
        self->m_dbgshim.UnregisterForRuntimeStartup(self->m_unregisterToken);
        self->m_unregisterToken = nullptr;
    }
}

// From dbgshim.cpp
static bool AreAllHandlesValid(HANDLE *handleArray, DWORD arrayLength)
{
    for (DWORD i = 0; i < arrayLength; i++)
    {
        HANDLE h = handleArray[i];
        if (h == INVALID_HANDLE_VALUE) // NOLINT(performance-no-int-to-ptr,cppcoreguidelines-pro-type-cstyle-cast)
        {
            return false;
        }
    }
    return true;
}

static HRESULT EnumerateCLRs(dbgshim_t &dbgshim, DWORD pid, HANDLE **ppHandleArray, LPWSTR **ppStringArray,
                             DWORD *pdwArrayLength, int tryCount)
{
    int numTries = 0;
    HRESULT hr = S_OK;

    while (numTries < tryCount)
    {
        hr = dbgshim.EnumerateCLRs(pid, ppHandleArray, ppStringArray, pdwArrayLength);

        // From dbgshim.cpp:
        // EnumerateCLRs uses the OS API CreateToolhelp32Snapshot which can return ERROR_BAD_LENGTH or
        // ERROR_PARTIAL_COPY. If we get either of those, we try wait 1/10th of a second try again (that
        // is the recommendation of the OS API owners).
        // In dbgshim the following condition is used:
        //  if ((hr != HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY)) && (hr != HRESULT_FROM_WIN32(ERROR_BAD_LENGTH)))
        // Since we may be attaching to the process which has not loaded coreclr yes, let's give it some time to load.
        if (SUCCEEDED(hr))
        {
            // Just return any other error or if no handles were found (which means the coreclr module wasn't found yet).
            if (*ppHandleArray != nullptr && *pdwArrayLength > 0)
            {

                // If EnumerateCLRs succeeded but any of the handles are INVALID_HANDLE_VALUE, then sleep and retry
                // also. This fixes a race condition where dbgshim catches the coreclr module just being loaded but
                // before g_hContinueStartupEvent has been initialized.
                if (AreAllHandlesValid(*ppHandleArray, *pdwArrayLength))
                {
                    return hr;
                }
                // Clean up memory allocated in EnumerateCLRs since this path it succeeded
                dbgshim.CloseCLREnumeration(*ppHandleArray, *ppStringArray, *pdwArrayLength);

                *ppHandleArray = nullptr;
                *ppStringArray = nullptr;
                *pdwArrayLength = 0;
            }
        }

        // No point in retrying in case of invalid arguments or no such process
        if (hr == E_INVALIDARG || hr == E_FAIL)
            return hr;

        // Sleep and retry enumerating the runtimes
        USleep(static_cast<unsigned long>(100 * 1000));
        numTries++;

        // if (m_canceled)
        // {
        //     break;
        // }
    }

    // Indicate a timeout
    hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);

    return hr;
}

static std::string GetCLRPath(dbgshim_t &dbgshim, DWORD pid, int timeoutSec = 3)
{
    HANDLE *pHandleArray = nullptr;
    LPWSTR *pStringArray = nullptr;
    DWORD dwArrayLength = 0;
    const int tryCount = timeoutSec * 10; // 100ms interval between attempts
    if (FAILED(EnumerateCLRs(dbgshim, pid, &pHandleArray, &pStringArray, &dwArrayLength, tryCount)) ||
        dwArrayLength == 0)
        return {};

    std::string result = to_utf8(pStringArray[0]);

    dbgshim.CloseCLREnumeration(pHandleArray, pStringArray, dwArrayLength);

    return result;
}

HRESULT ManagedDebugger::Startup(IUnknown *punk)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebug> iCorDebug;
    IfFailRet(punk->QueryInterface(IID_ICorDebug, reinterpret_cast<void **>(&iCorDebug)));

    IfFailRet(iCorDebug->Initialize());

    if (m_clrPath.empty())
        m_clrPath = GetCLRPath(m_dbgshim, m_processId);

    m_sharedCallbacksQueue = std::make_shared<CallbacksQueue>(*this);
    m_uniqueManagedCallback = std::make_unique<ManagedCallback>(*this, m_sharedCallbacksQueue);
    Status = iCorDebug->SetManagedHandler(m_uniqueManagedCallback.get());
    if (FAILED(Status))
    {
        iCorDebug->Terminate();
        m_uniqueManagedCallback.reset();
        m_sharedCallbacksQueue.reset();
        return Status;
    }

    ToRelease<ICorDebugProcess> iCorProcess;
    Status = iCorDebug->DebugActiveProcess(m_processId, FALSE, &iCorProcess);
    if (FAILED(Status))
    {
        iCorDebug->Terminate();
        m_uniqueManagedCallback.reset();
        m_sharedCallbacksQueue.reset();
        return Status;
    }

    WriteLock w_lock(m_debugProcessRWLock);

    m_iCorProcess = iCorProcess.Detach();
    m_iCorDebug = iCorDebug.Detach();

    w_lock.unlock();

#ifdef FEATURE_PAL
    GetWaitpid().SetupTrackingPID(m_processId);
#endif // FEATURE_PAL

    return S_OK;
}

static std::string EscapeShellArg(const std::string &arg)
{
    std::string s(arg);

    for (std::string::size_type i = 0; i < s.size(); ++i)
    {
        std::string::size_type count = 0;
        const char c = s.at(i);
        switch (c)
        {
        case '\"':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = '\"';
            break;
        case '\\':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = '\\';
            break;
        default:
            break;
        }
        i += count;
    }

    return s;
}

static bool IsDirExists(const char *const path)
{
    struct stat info{};

    if (stat(path, &info) != 0)
        return false;

    if ((info.st_mode & S_IFDIR) == 0U)
        return false;

    return true;
}

static void PrepareSystemEnvironmentArg(const std::map<std::string, std::string> &env, std::vector<char> &outEnv)
{
    // We need to append the environ values with keeping the current process environment block.
    // It works equal for any platrorms in coreclr CreateProcessW(), but not critical for Linux.
    std::map<std::string, std::string> envMap;
    if (GetSystemEnvironmentAsMap(envMap) != -1)
    {
        auto it = env.begin();
        auto end = env.end();
        // Override the system value (PATHs appending needs a complex implementation)
        while (it != end)
        {
            envMap[it->first] = it->second;
            ++it;
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
    }
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
    if (!m_cwd.empty())
    {
        if (!IsDirExists(m_cwd.c_str()) || !SetWorkDir(m_cwd))
            m_cwd.clear();
    }

    Status = m_ioredirect.exec([&]() -> HRESULT {
            IfFailRet(m_dbgshim.CreateProcessForLaunch(
                reinterpret_cast<WCHAR *>(const_cast<WCHAR*>(to_utf16(ss.str()).c_str())), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                TRUE, // Suspend process
                outEnv.empty() ? nullptr : outEnv.data(),
                m_cwd.empty() ? nullptr : reinterpret_cast<const WCHAR *>(to_utf16(m_cwd).c_str()),
                &m_processId, &resumeHandle));
            return Status;
        });

    if (FAILED(Status))
        return Status;

#ifdef FEATURE_PAL
    GetWaitpid().SetupTrackingPID(m_processId);
#endif // FEATURE_PAL

    IfFailRet(m_dbgshim.RegisterForRuntimeStartup(m_processId, ManagedDebugger::StartupCallback, this, &m_unregisterToken));

    // Resume the process so that StartupCallback can run
    IfFailRet(m_dbgshim.ResumeProcess(resumeHandle));
    m_dbgshim.CloseResumeHandle(resumeHandle);

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (!m_processAttachedCV.wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [this] { return m_processAttachedState == ProcessAttachedState::Attached; }))
    {
        IfFailRet(StartupCallbackHR);
        return E_FAIL;
    }

    pProtocol->EmitProcessEvent(PID{m_processId}, fileExec);

    return S_OK;
}

HRESULT ManagedDebugger::CheckNoProcess()
{
    const ReadLock r_lock(m_debugProcessRWLock);

    if (m_iCorProcess == nullptr)
        return S_OK;

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (m_processAttachedState == ProcessAttachedState::Attached)
        return E_FAIL; // Already attached
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
            break;

        if (m_iCorProcess == nullptr)
            return E_FAIL;

        BOOL procRunning = FALSE;
        if (SUCCEEDED(m_iCorProcess->IsRunning(&procRunning)) && procRunning == TRUE)
            m_iCorProcess->Stop(0);

        DisableAllBreakpointsAndSteppers();

        HRESULT Status = S_OK;
        if (FAILED(Status = m_iCorProcess->Detach()))
            LOGE("Process detach failed: 0x%08x", Status);

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
            break;

        if (m_iCorProcess == nullptr)
            return E_FAIL;

        BOOL procRunning = FALSE;
        if (SUCCEEDED(m_iCorProcess->IsRunning(&procRunning)) && procRunning == TRUE)
            m_iCorProcess->Stop(0);

        DisableAllBreakpointsAndSteppers();

        HRESULT Status = S_OK;
        if (SUCCEEDED(Status = m_iCorProcess->Terminate(0)))
        {
            m_processAttachedCV.wait(lockAttachedMutex, [this] { return m_processAttachedState == ProcessAttachedState::Unattached; });
            break;
        }

        LOGE("Process terminate failed: 0x%08x", Status);
        m_processAttachedState = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    } while (false);

    Cleanup();
    return S_OK;
}

void ManagedDebugger::Cleanup()
{
    m_sharedModules->CleanupAllModules();
    m_sharedEvalHelpers->Cleanup();
    m_sharedVariables->Clear();

    const WriteLock w_lock(m_debugProcessRWLock);

    assert((m_iCorProcess && m_iCorDebug && m_uniqueManagedCallback && m_sharedCallbacksQueue) ||
           (!m_iCorProcess && !m_iCorDebug && !m_uniqueManagedCallback && !m_sharedCallbacksQueue));

    if (m_iCorProcess == nullptr)
        return;

    m_iCorProcess.Free();

    m_iCorDebug->Terminate();
    m_iCorDebug.Free();

    if (m_uniqueManagedCallback->GetRefCount() > 0)
    {
        LOGW("ManagedCallback was not properly released by ICorDebug");
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
        return E_INVALIDARG; // Unable to find libcoreclr.so

    static constexpr uint32_t bufSize = 100;
    std::array<WCHAR, bufSize> pBuffer{};
    DWORD dwLength = 0;
    IfFailRet(m_dbgshim.CreateVersionStringFromModule(
        m_processId, reinterpret_cast<const WCHAR *>(to_utf16(m_clrPath).c_str()), pBuffer.data(), bufSize, &dwLength));

    ToRelease<IUnknown> pCordb;

    IfFailRet(m_dbgshim.CreateDebuggingInterfaceFromVersionEx(CorDebugVersion_4_0, pBuffer.data(), &pCordb));

    m_unregisterToken = nullptr;
    IfFailRet(Startup(pCordb));

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (!m_processAttachedCV.wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [this] { return m_processAttachedState == ProcessAttachedState::Attached; }))
        return E_FAIL;

    return S_OK;
}

HRESULT ManagedDebugger::GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> iCorThread;
    IfFailRet(m_iCorProcess->GetThread(static_cast<int>(threadId), &iCorThread));
    return m_uniqueBreakpoints->GetExceptionInfo(iCorThread, exceptionInfo);
}

HRESULT ManagedDebugger::SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                                 std::vector<Breakpoint> &breakpoints)
{
    LogFuncEntry();
    return m_uniqueBreakpoints->SetExceptionBreakpoints(exceptionBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::SetLineBreakpoints(const std::string &filename,
                                            const std::vector<LineBreakpoint> &lineBreakpoints,
                                            std::vector<Breakpoint> &breakpoints)
{
    LogFuncEntry();

    const bool haveProcess = HaveDebugProcess();
    return m_uniqueBreakpoints->SetLineBreakpoints(haveProcess, filename, lineBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::SetFuncBreakpoints(const std::vector<FuncBreakpoint> &funcBreakpoints,
                                            std::vector<Breakpoint> &breakpoints)
{
    LogFuncEntry();

    const bool haveProcess = HaveDebugProcess();
    return m_uniqueBreakpoints->SetFuncBreakpoints(haveProcess, funcBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::GetFrameLocation(ICorDebugFrame *pFrame, ThreadId threadId, FrameLevel level,
                                          StackFrame &stackFrame)
{
    HRESULT Status = S_OK;

    stackFrame = StackFrame(threadId, level, "");
    if (FAILED(TypePrinter::GetMethodName(pFrame, stackFrame.methodName)))
        stackFrame.methodName = "Unnamed method in optimized code";

    ToRelease<ICorDebugFunction> pFunc;
    IfFailRet(pFrame->GetFunction(&pFunc));

    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pFunc->GetModule(&pModule));

    uint32_t ilOffset = 0;
    SequencePoint sp;
    if (SUCCEEDED(m_sharedModules->GetFrameILAndSequencePoint(pFrame, ilOffset, sp)))
    {
        stackFrame.source = Source(sp.document);
        stackFrame.line = sp.startLine;
        stackFrame.column = sp.startColumn;
        stackFrame.endLine = sp.endLine;
        stackFrame.endColumn = sp.endColumn;
    }

    IfFailRet(GetModuleId(pModule, stackFrame.moduleId));

    return S_OK;
}

HRESULT ManagedDebugger::GetManagedStackTrace(ICorDebugThread *pThread, ThreadId threadId, FrameLevel startFrame,
                                              unsigned maxFrames, std::vector<StackFrame> &stackFrames, int &totalFrames)
{
    LogFuncEntry();

    HRESULT Status = S_OK;
    int currentFrame = -1;

    // CoreCLR native frame + at least one user's native frame
    static const std::string FrameCLRNativeText = "[Native Frames]";

    IfFailRet(WalkFrames(pThread,
        [&](FrameType frameType, ICorDebugFrame *pFrame)
        {
            currentFrame++;

            if (currentFrame < static_cast<int>(startFrame))
                return S_OK;
            if (maxFrames != 0 && currentFrame >= static_cast<int>(startFrame) + static_cast<int>(maxFrames))
                return S_OK;

            switch (frameType)
            {
            case FrameType::Unknown:
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, "?");
                break;
            case FrameType::CLRNative:
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, FrameCLRNativeText);
                break;
            case FrameType::CLRInternal:
            {
                ToRelease<ICorDebugInternalFrame> pInternalFrame;
                IfFailRet(pFrame->QueryInterface(IID_ICorDebugInternalFrame, reinterpret_cast<void **>(&pInternalFrame)));
                CorDebugInternalFrameType corFrameType;
                IfFailRet(pInternalFrame->GetFrameType(&corFrameType));
                std::string name = "[";
                name += GetInternalTypeName(corFrameType);
                name += "]";
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, name);
            }
            break;
            case FrameType::CLRManaged:
            {
                StackFrame stackFrame;
                GetFrameLocation(pFrame, threadId, FrameLevel{currentFrame}, stackFrame);
                stackFrames.push_back(stackFrame);
            }
            break;
            }

            return S_OK;
        }));

    totalFrames = currentFrame + 1;
    ExceptionInfo exceptionInfo;
    bool analyzeExceptions = true;
    if (!stackFrames.empty())
    {
        analyzeExceptions = analyzeExceptions && (stackFrames.front().line == 0);
    }

    if (!analyzeExceptions)
        return S_OK;

    // Sometimes Coreclr may return the empty stack frame in exception info
    // for some unknown reason. In that case the 2nd attempt is usually successful.
    // If even the 3rd attempt failed, there is almost no chances to get data successfuly.
    const int tries = 3;
    for (int tryCount = 0; tryCount < tries; tryCount++)
    {
        if (SUCCEEDED(GetExceptionInfo(threadId, exceptionInfo)))
        {
            std::stringstream ss(exceptionInfo.details.stackTrace);
            int countOfNewFrames = 0;
            int currentFrame = -1;
            const size_t sizeofStackFrame = stackFrames.size();

            // The stackTrace strings from ExceptionInfo usually looks like:
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
                    continue;

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
                    continue;

                beginpath++;
                if (beginpath >= lastcolon)
                    continue;

                // remove leading spaces and the first word ("at" for the case of English locale)
                size_t beginname = line.find_first_not_of(' ');
                if (beginname == std::string::npos)
                    continue;

                beginname = line.find_first_of(' ', beginname);
                if (beginname == std::string::npos)
                    continue;
                beginname++;

                // the function name ends with the last ')' before the beginning of fullpath
                size_t endname = line.find_last_of(')', beginpath);
                if (endname == std::string::npos)
                    continue;
                endname++;

                if (beginname >= endname)
                    continue;

                // look for the line number after the last colon
                const size_t beginlinenum = line.find_first_of("0123456789", lastcolon);
                const size_t endlinenum = line.find_first_not_of("0123456789", beginlinenum);
                if (beginlinenum == std::string::npos)
                    continue;

                currentFrame++;
                if (currentFrame < static_cast<int>(startFrame) || (maxFrames != 0 && currentFrame >= static_cast<int>(startFrame) + static_cast<int>(maxFrames)))
                    continue;

                const int l{std::stoi(line.substr(beginlinenum, endlinenum))};
                stackFrames.emplace_back(threadId, FrameLevel{currentFrame}, line.substr(beginname, endname - beginname));
                stackFrames.back().source = Source(line.substr(beginpath, lastcolon - beginpath));
                stackFrames.back().line = stackFrames.back().endLine = l;
                countOfNewFrames++;
            }

            if (countOfNewFrames > 0)
            {
                stackFrames.erase(stackFrames.begin(), stackFrames.begin() + sizeofStackFrame);
                totalFrames = currentFrame + 1;
                break;
            }
        }
    }

    return S_OK;
}

HRESULT ManagedDebugger::GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                                       std::vector<StackFrame> &stackFrames, int &totalFrames)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> pThread;
    if (SUCCEEDED(Status = m_iCorProcess->GetThread(static_cast<int>(threadId), &pThread)))
        return GetManagedStackTrace(pThread, threadId, startFrame, maxFrames, stackFrames, totalFrames);

    return Status;
}

HRESULT ManagedDebugger::GetVariables(uint32_t variablesReference, VariablesFilter filter, int start, int count,
                                      std::vector<Variable> &variables)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->GetVariables(m_iCorProcess, variablesReference, filter, start, count, variables);
}

HRESULT ManagedDebugger::GetScopes(FrameId frameId, std::vector<Scope> &scopes)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->GetScopes(m_iCorProcess, frameId, scopes);
}

HRESULT ManagedDebugger::Evaluate(FrameId frameId, const std::string &expression, Variable &variable,
                                  std::string &output)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->Evaluate(m_iCorProcess, frameId, expression, variable, output);
}

void ManagedDebugger::CancelEvalRunning()
{
    LogFuncEntry();

    m_sharedEvalWaiter->CancelEvalRunning();
}

HRESULT ManagedDebugger::SetVariable(const std::string &name, const std::string &value, uint32_t ref,
                                     std::string &output)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->SetVariable(m_iCorProcess, name, value, ref, output);
}

HRESULT ManagedDebugger::SetExpression(FrameId frameId, const std::string &expression, uint32_t evalFlags,
                                       const std::string &value, std::string &output)
{
    LogFuncEntry();

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->SetExpression(m_iCorProcess, frameId, expression, evalFlags, value, output);
}

void ManagedDebugger::SetJustMyCode(bool enable)
{
    m_justMyCode = enable;
    m_uniqueSteppers->SetJustMyCode(enable);
    m_uniqueBreakpoints->SetJustMyCode(enable);
}

void ManagedDebugger::SetStepFiltering(bool enable)
{
    m_stepFiltering = enable;
    m_uniqueSteppers->SetStepFiltering(enable);
}

void ManagedDebugger::InputCallback(IORedirectHelper::StreamType type, Utility::span<char> text)
{
    pProtocol->EmitOutputEvent(OutputEvent(type == IOSystem::Stderr ? OutputCategory::StdErr : OutputCategory::StdOut, {text.begin(), text.size()}));
}

} // namespace dncdbg
