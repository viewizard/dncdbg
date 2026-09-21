// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "debugger/manageddebugger.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/callbacksqueue.h"
#include "debugger/evalstackmachine.h"
#include "debugger/evaluator.h"
#include "debugger/frames.h"
#include "debugger/managedcallback.h"
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "debugger/variables.h"
#include "debuginfo/debuginfo.h"
#include "metadata/modules.h"
#include "protocol/dapio.h"
#include "utils/diagnostics_client.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/platform.h"
#include "utils/utf.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef __linux__
#include "utils/waitpid.h"
#elif (defined(__APPLE__) && defined(__MACH__))
#include "utils/kqueue.h"
#endif

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

bool IsDirExists(const char *const path)
{
    struct stat info{};

    return stat(path, &info) == 0 &&
           (info.st_mode & S_IFDIR) != 0U;
}

bool EqualCaseInsensitive(std::string_view lhs, std::string_view rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), [](char lhsChar, char rhsChar)
           {
               return std::tolower(static_cast<unsigned char>(lhsChar)) ==
                      std::tolower(static_cast<unsigned char>(rhsChar));
           });
}

// Normalize the DOTNET_DiagnosticPorts value to prevent the diagnostic port suspend usage during the debuggee
// process creation (it provokes a `configurationDone` command time out). The value may hold several port specs
// separated by ';', each port spec is a comma-separated list "<TARGET>[,<SUSPEND_POLICY>[,<SHARED_MEMORY>]]":
// "/diag/port.sock,suspend"   -> "/diag/port.sock,nosuspend"
// "/diag/port.sock"           -> "/diag/port.sock,nosuspend"
// "/diag/port.sock,nosuspend" -> keep as is
std::string AdjustDiagnosticPortsValue(const std::string &value)
{
    static constexpr char portsSeparator{';'};
    static constexpr char fieldsSeparator{','};
    static constexpr std::string_view suspend{"suspend"};
    static constexpr std::string_view nosuspend{"nosuspend"};

    std::string result;
    std::istringstream portsStream{value};
    std::string port;
    while (std::getline(portsStream, port, portsSeparator))
    {
        if (!result.empty())
        {
            result.push_back(portsSeparator);
        }

        bool portSpecFound{false};
        bool suspendPolicyFound{false};
        std::istringstream portSpecStream{port};
        std::string field;
        while (std::getline(portSpecStream, field, fieldsSeparator))
        {
            if (portSpecFound)
            {
                result.push_back(fieldsSeparator);
            }
            portSpecFound = true;

            if (EqualCaseInsensitive(field, suspend))
            {
                field.assign(nosuspend);
                suspendPolicyFound = true;
            }
            else if (EqualCaseInsensitive(field, nosuspend))
            {
                suspendPolicyFound = true;
            }
            result += field;
        }

        // The port spec does not define the suspend policy explicitly, add it.
        if (portSpecFound && !suspendPolicyFound)
        {
            result.push_back(fieldsSeparator);
            result += nosuspend;
        }
    }

    return result;
}

void PrepareSystemEnvironmentArg(const std::map<std::string, std::string> &env, std::vector<char> &outEnv)
{
    // Prevent diagnostic port suspend usage during debuggee process creation, since the diagnostics part suspends
    // the debuggee process at an early launch stage and provokes a `configurationDone` command time out.
    static const std::string diagnosticPortSuspendEnv{"DOTNET_DefaultDiagnosticPortSuspend"};
    static const std::string diagnosticPortsEnv{"DOTNET_DiagnosticPorts"};

    const auto appendEnvVariable = [&outEnv](const std::string &key, const std::string &value)
    {
        outEnv.insert(outEnv.end(), key.cbegin(), key.cend());
        outEnv.push_back('=');
        outEnv.insert(outEnv.end(), value.cbegin(), value.cend());
        outEnv.push_back('\0');
    };

    // We need to append the environment values while keeping the current process environment block.
    // It works equally for all platforms in coreclr CreateProcessW(), but is not critical for Linux.
    std::map<std::string, std::string> envMap;
    if (SUCCEEDED(GetSystemEnvironmentAsMap(envMap)))
    {
        // Override the system value (PATHs appending needs a complex implementation)
        for (const auto &pair : env)
        {
            const auto findEnv = envMap.find(pair.first);
            if (findEnv != envMap.cend())
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
            if (pair.first == diagnosticPortSuspendEnv)
            {
#ifdef _WIN32
                _putenv_s(diagnosticPortSuspendEnv.c_str(), "");
#else
                unsetenv(diagnosticPortSuspendEnv.c_str());
#endif
                DAPIO::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                    "Environment variable " + diagnosticPortSuspendEnv + " skipped."));
                continue;
            }

            if (pair.first == diagnosticPortsEnv)
            {
                const std::string adjustedValue{AdjustDiagnosticPortsValue(pair.second)};
                if (adjustedValue != pair.second)
                {
                    DAPIO::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                        "Environment variable DOTNET_DiagnosticPorts value adjusted to '" + adjustedValue + "'."));
                }
#ifdef _WIN32
                _putenv_s(diagnosticPortsEnv.c_str(), adjustedValue.c_str());
#else
                setenv(diagnosticPortsEnv.c_str(), adjustedValue.c_str(), 1);
#endif
                appendEnvVariable(pair.first, adjustedValue);
                continue;
            }

            appendEnvVariable(pair.first, pair.second);
        }
        outEnv.push_back('\0');
    }
    else
    {
        for (const auto &pair : env)
        {
            if (pair.first == diagnosticPortSuspendEnv)
            {
                DAPIO::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                    "Environment variable " + diagnosticPortSuspendEnv + " skipped."));
                continue;
            }

            if (pair.first == diagnosticPortsEnv)
            {
                const std::string adjustedValue{AdjustDiagnosticPortsValue(pair.second)};
                if (adjustedValue != pair.second)
                {
                    DAPIO::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                        "Environment variable DOTNET_DiagnosticPorts value adjusted to '" + adjustedValue + "'."));
                }
                appendEnvVariable(pair.first, adjustedValue);
                continue;
            }

            appendEnvVariable(pair.first, pair.second);
        }
        outEnv.push_back('\0');
    }
}

void ResumeRuntime(uint32_t processId)
{
    // The runtime may not be ready to accept diagnostics commands yet,
    // so retry the resume a few times before giving up.
    static constexpr unsigned long initialSleepTime = 50000UL; // 0.05 sec
    unsigned long sleepTime = initialSleepTime;
    static constexpr uint8_t retriesLimit = 5;
    uint8_t retriesLeft = retriesLimit;
    while (FAILED(DiagnosticsClient::ResumeRuntime(processId)) && retriesLeft > 0)
    {
        USleep(sleepTime);
        sleepTime *= 2;
        retriesLeft--;
    }
}

} // unnamed namespace

// Caller must hold m_debugProcessRWLock.
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

// Caller must hold m_debugProcessRWLock.
void ManagedDebugger::DisableAllBreakpointsAndSteppers()
{
    m_uniqueSteppers->DisableAllSteppers(m_trProcess); // Async stepper could have breakpoints active, disable them first.
    m_sharedBreakpoints->DeleteAll();
    Breakpoints::DisableAll(m_trProcess); // Last one, disable all breakpoints on all domains, even if we don't hold them.
}

void ManagedDebugger::SetLastStoppedThread(ICorDebugThread *pThread)
{
    SetLastStoppedThreadId(GetThreadId(pThread));
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
      m_sharedEvalStackMachine(std::make_shared<EvalStackMachine>()),
      m_sharedVariables(std::make_shared<Variables>(m_sharedEvalStackMachine)),
      m_uniqueSteppers(std::make_unique<Steppers>()),
      m_sharedBreakpoints(std::make_shared<Breakpoints>(m_sharedEvalStackMachine)),
      m_sharedCallbacksQueue(nullptr),
      m_uniqueManagedCallback(nullptr),
      m_ioredirect([this](IORedirect::StreamType type, gsl::span<char> text)
            {
                InputCallback(type, text);
            })
{
}

ManagedDebugger::~ManagedDebugger() = default;

HRESULT ManagedDebugger::Initialize()
{
    // TODO: Report capabilities and check client support
    m_startMethod = StartMethod::None;
    return S_OK;
}

HRESULT ManagedDebugger::ConfigurationDone()
{
    FrameId::invalidate();

    switch (m_startMethod)
    {
    case StartMethod::Launch:
        return RunProcess(m_execPath, m_execArgs);
    case StartMethod::Attach:
        return AttachToProcess();
    default:
        assert(false);
        return E_FAIL;
    }
}

HRESULT ManagedDebugger::Attach(DWORD pid)
{
    m_startMethod = StartMethod::Attach;
    m_processId = pid;
    return S_OK;
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
    return S_OK;
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
        case StartMethod::None: // The debugger was initialized, but no process was launched or attached.
            return S_OK;
        default:
            assert(false);
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
        assert(false);
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

HRESULT ManagedDebugger::StepCommand(ThreadId threadId, StepType stepType, bool singleThread)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
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

    // Note, the continued event is emitted only on success, so we don't report continuation
    // when the process failed to resume. On failure, disable all steppers, since we set up
    // a step above but the process didn't actually resume.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_trProcess, threadId, singleThread)))
    {
        m_uniqueSteppers->DisableAllSteppers(m_trProcess);
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }
    else
    {
        m_sharedVariables->Cleanup();
        FrameId::invalidate();                             // Clear all created during break frames.
        DAPIO::EmitContinuedEvent(threadId, singleThread); // DAP needs thread ID.
    }

    return Status;
}

HRESULT ManagedDebugger::Continue(ThreadId threadId, bool singleThread)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
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

    // Note, the continued event is emitted only on success, so we don't report continuation
    // when the process failed to resume.
    if (FAILED(Status = m_sharedCallbacksQueue->Continue(m_trProcess, threadId, singleThread)))
    {
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }
    else
    {
        m_sharedVariables->Cleanup();
        FrameId::invalidate();                             // Clear all created during break frames.
        DAPIO::EmitContinuedEvent(threadId, singleThread); // DAP needs thread ID.
    }

    return Status;
}

bool ManagedDebugger::IsProcessRunning()
{
    const ReadLock r_lock(m_debugProcessRWLock);

    if (FAILED(CheckDebugProcess()) ||
        EvalWaiter::IsEvalRunning())
    {
        return false;
    }

    return m_sharedCallbacksQueue->IsRunning();
}

HRESULT ManagedDebugger::Pause(ThreadId lastStoppedThread)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedCallbacksQueue->Pause(m_trProcess, lastStoppedThread);
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the Threads namespace functions, so it is intentionally kept non-static.
HRESULT ManagedDebugger::GetThreads(std::vector<Thread> &threads) // NOLINT(readability-convert-member-functions-to-static)
{
    return Threads::GetThreads(threads);
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
        ss << '\n';
        DAPIO::EmitOutputEvent({OutputCategory::StdErr, ss.str()});
        self->StartupCallbackHR = hr;
        return;
    }

    self->StartupCallbackHR = self->Startup(pCordb);

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

    return S_OK;
}

HRESULT ManagedDebugger::RunProcess(const std::string &fileExec, const std::vector<std::string> &execArgs)
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    // Reset the startup callback error from a previous launch attempt, if any.
    StartupCallbackHR = S_OK;

    std::ostringstream ss;
    ss << "\"" << fileExec << "\"";
    for (const std::string &arg : execArgs)
    {
        if (arg.empty())
        {
            continue;
        }

        // https://github.com/dotnet/diagnostics/blob/3a9d1f8a7ba2a6cae33f6953af77086bfbe02906/src/shared/pal/src/thread/process.cpp#L3898-L3902
        // Note: dbgshim uses different logic for building arguments compared to Windows.
        // dbgshim uses simpler logic and cannot properly handle arguments ending with `\"`, for example "D:\\path\\to to\\dir\\".
        // When wrapped in quotes, dbgshim interprets the trailing `\"` as an escaped quote, resulting in
        // `D:\path\to to\dir\"` which corrupts the next argument parsing.
        if (arg.back() == '\\' && arg.find(' ') == std::string::npos)
        {
            ss << " " << arg;
        }
        else if (arg.back() == '\\')
        {
#ifdef _WIN32
            ss << " \"" << arg << R"(\")";
#else
            LOGE(log << "Arguments with spaces and a trailing backslash are not supported by dbgshim: " << arg);
#endif
        }
        else
        {
            ss << " \"" << arg << "\"";
        }
    }

    HANDLE resumeHandle = nullptr; // Fake thread handle for the process resume

    std::vector<char> outEnv;
    PrepareSystemEnvironmentArg(m_env, outEnv);

    // cwd in launch.json set working directory for debugger https://code.visualstudio.com/docs/python/debugging#_cwd
    if (!m_cwd.empty() &&
        (!IsDirExists(m_cwd.c_str()) || !SetWorkDir(m_cwd)))
    {
        m_cwd.clear();
    }

    m_ioredirect.Exec([&]
        {
            Status = m_dbgshim.GetCreateProcessForLaunch()(
                const_cast<WCHAR *>(to_utf16(ss.str()).c_str()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                TRUE, // Suspend process
                outEnv.empty() ? nullptr : outEnv.data(),
                m_cwd.empty() ? nullptr : reinterpret_cast<const WCHAR *>(to_utf16(m_cwd).c_str()),
                &m_processId, &resumeHandle);
        });

    if (FAILED(Status))
    {
        return Status;
    }

#if (defined(__APPLE__) && defined(__MACH__))
    MacKqueue::SetupTrackingPID(static_cast<pid_t>(m_processId));
#elif __linux__
    WaitpidHook::SetupTrackingPID(static_cast<pid_t>(m_processId));
#endif

    IfFailRet(m_dbgshim.GetRegisterForRuntimeStartup()(m_processId, ManagedDebugger::StartupCallback, this, &m_unregisterToken));

    // Resume the process so that StartupCallback can run.
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
    }
    while (false);

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
    }
    while (false);

    Cleanup();
    return S_OK;
}

void ManagedDebugger::Cleanup()
{
    DebugInfo::Cleanup();
    m_sharedVariables->Cleanup();
    EvalExec::Cleanup();
    SystemTypes::Cleanup();
    EvalWaiter::Cleanup();
    TypeProxy::Cleanup();
    Evaluator::Cleanup();
    Modules::Cleanup();
    Threads::Cleanup();

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

    // Reset the startup callback error from a previous attach attempt, if any.
    StartupCallbackHR = S_OK;

    IfFailRet(m_dbgshim.GetRegisterForRuntimeStartup()(m_processId, ManagedDebugger::StartupCallback, this, &m_unregisterToken));

    // Resume the runtime so that StartupCallback can run.
    ResumeRuntime(m_processId);

    DAPIO::EmitProcessEvent(m_processId, "dotnet", m_startMethod);

    std::unique_lock<std::mutex> lockAttachedMutex(m_processAttachedMutex);
    if (!m_processAttachedCV.wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [this] { return m_processAttachedState == ProcessAttachedState::Attached; }))
    {
        IfFailRet(StartupCallbackHR);
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

HRESULT ManagedDebugger::SetSourceBreakpoints(const Source &source,
                                              const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                              std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return m_sharedBreakpoints->SetSourceBreakpoints(haveProcess, source, sourceBreakpoints, breakpoints);
}

HRESULT ManagedDebugger::SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                                std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return m_sharedBreakpoints->SetFunctionBreakpoints(haveProcess, functionBreakpoints, breakpoints);
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
        return GetStackFrames(trThread, threadId, startFrame, maxFrames, IsJustMyCode(), stackFrames);
    }

    return Status;
}

HRESULT ManagedDebugger::GetVariables(uint32_t variablesReference, std::vector<Variable> &variables)
{
    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return m_sharedVariables->GetVariables(m_trProcess, variablesReference, variables);
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

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the static EvalWaiter, so it is intentionally kept non-static.
void ManagedDebugger::CancelEvalRunning() // NOLINT(readability-convert-member-functions-to-static)
{
    EvalWaiter::CancelEvalRunning();
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
    Evaluator::SetJustMyCode(enable);
}

void ManagedDebugger::SetStepFiltering(bool enable)
{
    m_stepFiltering = enable;
    m_uniqueSteppers->SetStepFiltering(enable);
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only forwards
// the calls to the EvalExec and Evaluator namespace functions, so it is intentionally kept non-static.
void ManagedDebugger::SetEvalFlags(uint32_t evalFlags) // NOLINT(readability-convert-member-functions-to-static)
{
    EvalExec::SetEvalFlags(evalFlags);
    Evaluator::SetEvalFlags(evalFlags);
}

void ManagedDebugger::InputCallback(IORedirect::StreamType type, gsl::span<char> text)
{
    DAPIO::EmitOutputEvent(OutputEvent(type == IORedirect::StreamType::Stderr ? OutputCategory::StdErr : OutputCategory::StdOut, {text.data(), text.size()}));
    m_remoteConsoleServer.SendData(text);
}

void ManagedDebugger::WriteStdin(gsl::span<const char> text)
{
    m_ioredirect.WriteStdin(text);
}

bool ManagedDebugger::InitializeRemoteConsoleServer(int port)
{
    return m_remoteConsoleServer.Initialize(port,
        [this](gsl::span<char> text)
        {
            m_ioredirect.WriteStdin(text);
        });
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the Modules namespace, so it is intentionally kept non-static.
void ManagedDebugger::GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules) // NOLINT(readability-convert-member-functions-to-static)
{
    Modules::GetModules(startModule, moduleCount, modules, totalModules);
}

HRESULT ManagedDebugger::GetGotoTarget(const Source &source, int32_t line, int32_t column, std::vector<GotoTarget> &targets, std::string &output)
{
    HRESULT Status = S_OK;

    m_targets.clear();
    m_intTargets.clear();

    IfFailRet(DebugInfo::GetGotoTarget(source, line, column, m_targets, m_intTargets, output));

    targets = m_targets;

    return S_OK;
}

HRESULT ManagedDebugger::Goto(ThreadId threadId, uint32_t targetId, std::string &output)
{
    if (m_intTargets.empty())
    {
        return E_INVALIDARG;
    }

    bool targetFound = false;
    uint32_t targetIndex = 0;
    for (const auto &target : m_intTargets)
    {
        if (targetId != target.id)
        {
            targetIndex++;
            continue;
        }

        targetFound = true;
        break;
    }

    if (!targetFound)
    {
        return E_INVALIDARG;
    }

    const ReadLock r_lock(m_debugProcessRWLock);
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
    {
        // Important! Abort all evals before 'Goto' in protocol, during eval we have inconsistent thread state.
        LOGE(log << "Can't 'Goto' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (m_sharedCallbacksQueue->IsRunning())
    {
        LOGI(log << "Can't 'Goto', process already running.");
        return E_FAIL;
    }

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(m_trProcess->GetThread(static_cast<int>(threadId), &trThread));
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(trThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(trFrame->GetFunction(&trFunc));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    const GotoTargetInternal &target = m_intTargets.at(targetIndex);

    if (target.modAddress != modAddress ||
        target.methodToken != methodToken)
    {
        output = "Error setting next statement. The next statement cannot be set to another function.";
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    IfFailRet(trILFrame->SetIP(target.ilOffset));

    m_sharedVariables->Cleanup();
    FrameId::invalidate();               // Clear all created during break frames.

    SetLastStoppedThreadId(threadId);

    return S_OK;
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the DebugInfo namespace functions, so it is intentionally kept non-static.
HRESULT ManagedDebugger::GetSourceContent(const Source &source, std::string &sourceContent) // NOLINT(readability-convert-member-functions-to-static)
{
    return DebugInfo::GetSourceContent(source, sourceContent);
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the DebugInfo namespace functions, so it is intentionally kept non-static.
void ManagedDebugger::GetLoadedSources(std::vector<Source> &sources) // NOLINT(readability-convert-member-functions-to-static)
{
    DebugInfo::GetLoadedSources(sources);
}

// Note, this method is part of the ManagedDebugger public API (see dap.cpp); it only delegates
// the call to the DebugInfo namespace functions, so it is intentionally kept non-static.
HRESULT ManagedDebugger::GetBreakpointLocations(const Source &source, const BreakpointLocation &rangeToSearch, // NOLINT(readability-convert-member-functions-to-static)
                                                std::vector<BreakpointLocation> &locations)
{
    return DebugInfo::GetBreakpointLocations(source, rangeToSearch, locations);
}

} // namespace dncdbg
