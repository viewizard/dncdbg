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
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/frames.h"
#include "debugger/managedcallback.h"
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "debugger/variables.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/types.h"
#include "metadata/modules.h"
#include "protocol/dap_events.h"
#include "utils/dbgshim.h"
#include "utils/diagnostics_client.h"
#include "utils/hresult.h"
#include "utils/ioredirect.h"
#include "utils/logger.h"
#include "utils/platform.h"
#include "utils/remote_console.h"
#include "utils/rwlock.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef __linux__
#include "utils/waitpid.h"
#elif (defined(__APPLE__) && defined(__MACH__))
#include "utils/kqueue.h"
#endif

namespace dncdbg::ManagedDebugger
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

void NotifyProcessExited();
void InputCallback(IORedirect::StreamType type, gsl::span<char> text);

enum class ProcessAttachedState : uint8_t
{
    Attached,
    Unattached
};

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
                DAP::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                    "Environment variable " + diagnosticPortSuspendEnv + " skipped."));
                continue;
            }

            if (pair.first == diagnosticPortsEnv)
            {
                const std::string adjustedValue{AdjustDiagnosticPortsValue(pair.second)};
                if (adjustedValue != pair.second)
                {
                    DAP::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
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
                DAP::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
                    "Environment variable " + diagnosticPortSuspendEnv + " skipped."));
                continue;
            }

            if (pair.first == diagnosticPortsEnv)
            {
                const std::string adjustedValue{AdjustDiagnosticPortsValue(pair.second)};
                if (adjustedValue != pair.second)
                {
                    DAP::EmitOutputEvent(OutputEvent(OutputCategory::StdOut,
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

// ManagedDebugger internal state accessors.

std::mutex &GetProcessAttachedMutex()
{
    // Note, if both the debug process RWLock and this mutex are locked, the RWLock must be locked first.
    static std::mutex processAttachedMutex;
    return processAttachedMutex;
}

std::condition_variable &GetProcessAttachedCV()
{
    static std::condition_variable processAttachedCV;
    return processAttachedCV;
}

ProcessAttachedState &GetProcessAttachedState()
{
    static ProcessAttachedState processAttachedState{ProcessAttachedState::Unattached};
    return processAttachedState;
}

StartMethod &GetStartMethod()
{
    static StartMethod startMethod{StartMethod::None};
    return startMethod;
}

std::string &GetExecPath()
{
    static std::string execPath;
    return execPath;
}

std::vector<std::string> &GetExecArgs()
{
    static std::vector<std::string> execArgs;
    return execArgs;
}

std::string &GetCwd()
{
    static std::string cwd;
    return cwd;
}

std::map<std::string, std::string> &GetEnv()
{
    static std::map<std::string, std::string> env;
    return env;
}

std::unique_ptr<ManagedCallback> &GetManagedCallback()
{
    static std::unique_ptr<ManagedCallback> uniqueManagedCallback = std::make_unique<ManagedCallback>([]
    {
        NotifyProcessExited();
    });
    return uniqueManagedCallback;
}

RWLock &GetDebugProcessRWLock()
{
    static RWLock debugProcessRWLock;
    return debugProcessRWLock;
}

ToRelease<ICorDebug> &GetTrDebug()
{
    static ToRelease<ICorDebug> trDebug;
    return trDebug;
}

ToRelease<ICorDebugProcess> &GetTrProcess()
{
    static ToRelease<ICorDebugProcess> trProcess;
    return trProcess;
}

void *&GetUnregisterToken()
{
    static void *unregisterToken{nullptr};
    return unregisterToken;
}

DWORD &GetProcessId()
{
    static DWORD processId{0};
    return processId;
}

dbgshim_t &GetDbgshim()
{
    static dbgshim_t dbgshim;
    return dbgshim;
}

IORedirect &GetIORedirect()
{
    static IORedirect ioredirect([](IORedirect::StreamType type, gsl::span<char> text)
    {
        InputCallback(type, text);
    });
    return ioredirect;
}

RemoteConsoleServer &GetRemoteConsoleServer()
{
    static RemoteConsoleServer remoteConsoleServer;
    return remoteConsoleServer;
}

std::vector<GotoTargetInternal> &GetIntTargets()
{
    static std::vector<GotoTargetInternal> intTargets;
    return intTargets;
}

std::atomic<HRESULT> &GetStartupCallbackHR()
{
    static std::atomic<HRESULT> startupCallbackHR{S_OK}; // Written by the startup callback thread, read by the caller thread.
    return startupCallbackHR;
}

// Caller must hold the debug process RWLock.
HRESULT CheckDebugProcess()
{
    if (GetTrProcess() == nullptr)
    {
        return E_FAIL;
    }

    // The process may have exited or detached while the process object is still not freed and holds an
    // invalid object.
    // Note, we can't hold this lock, since this could deadlock execution at ICorDebugManagedCallback::ExitProcess call.
    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (GetProcessAttachedState() == ProcessAttachedState::Unattached)
    {
        return E_FAIL;
    }
    lockAttachedMutex.unlock();

    return S_OK;
}

bool HaveDebugProcess()
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    return SUCCEEDED(CheckDebugProcess());
}

void NotifyProcessCreated()
{
    std::unique_lock<std::mutex> lock(GetProcessAttachedMutex());
    GetProcessAttachedState() = ProcessAttachedState::Attached;
    lock.unlock();
    GetProcessAttachedCV().notify_one();
}

void NotifyProcessExited()
{
    std::unique_lock<std::mutex> lock(GetProcessAttachedMutex());
    GetProcessAttachedState() = ProcessAttachedState::Unattached;
    lock.unlock();
    GetProcessAttachedCV().notify_all();
}

void InputCallback(IORedirect::StreamType type, gsl::span<char> text)
{
    DAP::EmitOutputEvent(OutputEvent(type == IORedirect::StreamType::Stderr ? OutputCategory::StdErr : OutputCategory::StdOut, {text.data(), text.size()}));
    GetRemoteConsoleServer().SendData(text);
}

void Cleanup()
{
    Steppers::Cleanup();
    Breakpoints::Cleanup();
    DebugInfo::Cleanup();
    Variables::Cleanup();
    EvalExec::Cleanup();
    SystemTypes::Cleanup();
    EvalWaiter::Cleanup();
    TypeProxy::Cleanup();
    Walkers::Cleanup();
    Modules::Cleanup();
    Threads::Cleanup();
    CallbacksQueue::Cleanup();

    const WriteLock w_lock(GetDebugProcessRWLock());

    assert((GetTrProcess() && GetTrDebug()) ||
           (!GetTrProcess() && !GetTrDebug()));

    if (GetTrProcess() == nullptr)
    {
        return;
    }

    GetTrProcess().Free();

    GetTrDebug()->Terminate();
    GetTrDebug().Free();
}

HRESULT Startup(IUnknown *punk)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebug> trDebug;
    IfFailRet(punk->QueryInterface(IID_ICorDebug, reinterpret_cast<void **>(&trDebug)));

    IfFailRet(trDebug->Initialize());

    if (FAILED(Status = trDebug->SetManagedHandler(GetManagedCallback().get())))
    {
        trDebug->Terminate();
        return Status;
    }

    ToRelease<ICorDebugProcess> trProcess;
    if (FAILED(Status = trDebug->DebugActiveProcess(GetProcessId(), FALSE, &trProcess)))
    {
        trDebug->Terminate();
        return Status;
    }

    WriteLock w_lock(GetDebugProcessRWLock());

    GetTrProcess() = trProcess.Detach();
    GetTrDebug() = trDebug.Detach();

    w_lock.unlock();

    return S_OK;
}

// Should be called by dbgshim RegisterForRuntimeStartup().
void StartupCallback(IUnknown *pCordb, void * /*parameter*/, HRESULT hr)
{
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
        DAP::EmitOutputEvent({OutputCategory::StdErr, ss.str()});
        GetStartupCallbackHR() = hr;
        return;
    }

    GetStartupCallbackHR() = Startup(pCordb);

    if (GetUnregisterToken() != nullptr)
    {
        GetDbgshim().GetUnregisterForRuntimeStartup()(GetUnregisterToken());
        GetUnregisterToken() = nullptr;
    }
}

HRESULT CheckNoProcess()
{
    const ReadLock r_lock(GetDebugProcessRWLock());

    if (GetTrProcess() == nullptr)
    {
        return S_OK;
    }

    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (GetProcessAttachedState() == ProcessAttachedState::Attached)
    {
        return E_FAIL; // Already attached
    }
    lockAttachedMutex.unlock();

    Cleanup();
    return S_OK;
}

HRESULT RunProcess(const std::string &fileExec, const std::vector<std::string> &execArgs)
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    // Reset the startup callback error from a previous launch attempt, if any.
    GetStartupCallbackHR() = S_OK;

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
    PrepareSystemEnvironmentArg(GetEnv(), outEnv);

    // cwd in launch.json set working directory for debugger https://code.visualstudio.com/docs/python/debugging#_cwd
    if (!GetCwd().empty() &&
        (!IsDirExists(GetCwd().c_str()) || !SetWorkDir(GetCwd())))
    {
        GetCwd().clear();
    }

    GetIORedirect().Exec([&]
        {
            Status = GetDbgshim().GetCreateProcessForLaunch()(
                const_cast<WCHAR *>(to_utf16(ss.str()).c_str()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                TRUE, // Suspend process
                outEnv.empty() ? nullptr : outEnv.data(),
                GetCwd().empty() ? nullptr : reinterpret_cast<const WCHAR *>(to_utf16(GetCwd()).c_str()),
                &GetProcessId(), &resumeHandle);
        });

    if (FAILED(Status))
    {
        return Status;
    }

#if (defined(__APPLE__) && defined(__MACH__))
    MacKqueue::SetupTrackingPID(static_cast<pid_t>(GetProcessId()));
#elif __linux__
    WaitpidHook::SetupTrackingPID(static_cast<pid_t>(GetProcessId()));
#endif

    IfFailRet(GetDbgshim().GetRegisterForRuntimeStartup()(GetProcessId(), StartupCallback, nullptr, &GetUnregisterToken()));

    // Resume the process so that StartupCallback can run.
    IfFailRet(GetDbgshim().GetResumeProcess()(resumeHandle));
    GetDbgshim().GetCloseResumeHandle()(resumeHandle);

    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [] { return GetProcessAttachedState() == ProcessAttachedState::Attached; }))
    {
        IfFailRet(GetStartupCallbackHR());
        return E_FAIL;
    }

    DAP::EmitProcessEvent(GetProcessId(), fileExec, GetStartMethod());

    return S_OK;
}

HRESULT AttachToProcess()
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    // Reset the startup callback error from a previous attach attempt, if any.
    GetStartupCallbackHR() = S_OK;

    IfFailRet(GetDbgshim().GetRegisterForRuntimeStartup()(GetProcessId(), StartupCallback, nullptr, &GetUnregisterToken()));

    // Resume the runtime so that StartupCallback can run.
    ResumeRuntime(GetProcessId());

    DAP::EmitProcessEvent(GetProcessId(), "dotnet", GetStartMethod());

    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, startupWaitTimeout,
                                      [] { return GetProcessAttachedState() == ProcessAttachedState::Attached; }))
    {
        IfFailRet(GetStartupCallbackHR());
        return E_FAIL;
    }

    return S_OK;
}

HRESULT DetachFromProcess()
{
    do
    {
        const ReadLock r_lock(GetDebugProcessRWLock());
        const std::scoped_lock<std::mutex> guardAttachedMutex(GetProcessAttachedMutex());
        if (GetProcessAttachedState() == ProcessAttachedState::Unattached)
        {
            break;
        }

        if (GetTrProcess() == nullptr)
        {
            return E_FAIL;
        }

        BOOL procRunning = FALSE;
        if (SUCCEEDED(GetTrProcess()->IsRunning(&procRunning)) && procRunning == TRUE)
        {
            GetTrProcess()->Stop(0);
        }

        Steppers::DisableAll(GetTrProcess()); // Disable steppers first: an async stepper could have breakpoints active.
        Breakpoints::DisableAll(GetTrProcess()); // Disable breakpoints last, on all domains, even the ones we don't hold.

        HRESULT Status = S_OK;
        if (FAILED(Status = GetTrProcess()->Detach()))
        {
            LOGE(log << "Process detach failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        }

        GetProcessAttachedState() = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    }
    while (false);

    Cleanup();
    return S_OK;
}

HRESULT TerminateProcess()
{
    do
    {
        const ReadLock r_lock(GetDebugProcessRWLock());
        std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
        if (GetProcessAttachedState() == ProcessAttachedState::Unattached)
        {
            break;
        }

        if (GetTrProcess() == nullptr)
        {
            return E_FAIL;
        }

        BOOL procRunning = FALSE;
        if (SUCCEEDED(GetTrProcess()->IsRunning(&procRunning)) && procRunning == TRUE)
        {
            GetTrProcess()->Stop(0);
        }

        Steppers::DisableAll(GetTrProcess()); // Disable steppers first: an async stepper could have breakpoints active.
        Breakpoints::DisableAll(GetTrProcess()); // Disable breakpoints last, on all domains, even the ones we don't hold.

        HRESULT Status = S_OK;
        if (SUCCEEDED(Status = GetTrProcess()->Terminate(0)))
        {
            GetProcessAttachedCV().wait(lockAttachedMutex, [] { return GetProcessAttachedState() == ProcessAttachedState::Unattached; });
            break;
        }

        LOGE(log << "Process terminate failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        GetProcessAttachedState() = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    }
    while (false);

    Cleanup();
    return S_OK;
}

} // unnamed namespace

void Initialize()
{
    // Force the internal state construction; the dbgshim_t construction may throw.
    GetDbgshim();
    GetIORedirect();
    GetRemoteConsoleServer();
    GetManagedCallback();
    CallbacksQueue::Initialize([]
    {
        NotifyProcessCreated();
    });
}

void Shutdown()
{
    // Note, the callback is owned by the debugger for its whole lifetime,
    // so ICorDebug must release it before the debugger state is destroyed.
    if (GetManagedCallback()->GetRefCount() > 0)
    {
        LOGW(log << "ManagedCallback was not properly released by ICorDebug");
    }
    CallbacksQueue::Shutdown();
}

HRESULT Attach(DWORD pid)
{
    GetStartMethod() = StartMethod::Attach;
    Threads::SetProcessAttached(true);
    GetProcessId() = pid;
    return S_OK;
}

HRESULT Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
               const std::map<std::string, std::string> &env, const std::string &cwd)
{
    GetStartMethod() = StartMethod::Launch;
    Threads::SetProcessAttached(false);
    GetExecPath() = fileExec;
    GetExecArgs() = execArgs;
    GetCwd() = cwd;
    GetEnv() = env;
    return S_OK;
}

HRESULT ConfigurationDone()
{
    FrameId::invalidate();

    switch (GetStartMethod())
    {
    case StartMethod::Launch:
        return RunProcess(GetExecPath(), GetExecArgs());
    case StartMethod::Attach:
        return AttachToProcess();
    default:
        assert(false);
        return E_FAIL;
    }
}

HRESULT Disconnect(DisconnectAction action)
{
    bool terminate = false;
    switch (action)
    {
    case DisconnectAction::Default:
        switch (GetStartMethod())
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
        if (GetStartMethod() != StartMethod::Attach)
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
            DAP::EmitTerminatedEvent();
        }

        return Status;
    }

    return TerminateProcess();
}

HRESULT StepCommand(ThreadId threadId, StepType stepType, bool singleThread)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
    {
        // Important! Abort all evals before 'Step' in protocol: during an eval, the thread state is inconsistent.
        LOGE(log << "Can't 'Step' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (CallbacksQueue::IsRunning())
    {
        LOGW(log << "Can't 'Step', process already running.");
        return E_FAIL;
    }

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(GetTrProcess()->GetThread(static_cast<int>(threadId), &trThread));
    IfFailRet(Steppers::SetupStep(trThread, stepType));

    // Note, the continued event is emitted only on success, so we don't report continuation
    // when the process failed to resume. On failure, disable all steppers, since we set up
    // a step above but the process didn't actually resume.
    if (FAILED(Status = CallbacksQueue::Continue(GetTrProcess(), threadId, singleThread)))
    {
        Steppers::DisableAll(GetTrProcess());
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }
    else
    {
        Variables::Cleanup();
        FrameId::invalidate();                             // Clear all frames created during the break.
        DAP::EmitContinuedEvent(threadId, singleThread); // DAP needs thread ID.
    }

    return Status;
}

HRESULT Continue(ThreadId threadId, bool singleThread)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
    {
        // Important! Abort all evals before 'Continue' in protocol: during an eval, the thread state is inconsistent.
        LOGE(log << "Can't 'Continue' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (CallbacksQueue::IsRunning())
    {
        LOGI(log << "Can't 'Continue', process already running.");
        return S_OK; // Send an 'OK' response, but don't generate a continued event.
    }

    // Note, the continued event is emitted only on success, so we don't report continuation
    // when the process failed to resume.
    if (FAILED(Status = CallbacksQueue::Continue(GetTrProcess(), threadId, singleThread)))
    {
        LOGE(log << "Continue failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }
    else
    {
        Variables::Cleanup();
        FrameId::invalidate();                             // Clear all frames created during the break.
        DAP::EmitContinuedEvent(threadId, singleThread); // DAP needs thread ID.
    }

    return Status;
}

bool IsProcessRunning()
{
    const ReadLock r_lock(GetDebugProcessRWLock());

    if (FAILED(CheckDebugProcess()) ||
        EvalWaiter::IsEvalRunning())
    {
        return false;
    }

    return CallbacksQueue::IsRunning();
}

HRESULT Pause(ThreadId lastStoppedThread)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return CallbacksQueue::Pause(GetTrProcess(), lastStoppedThread);
}

ThreadId GetLastStoppedThreadId()
{
    return Threads::GetLastStoppedThreadId();
}

HRESULT GetThreads(std::vector<Thread> &threads)
{
    return Threads::GetThreads(threads);
}

HRESULT GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(GetTrProcess()->GetThread(static_cast<int>(threadId), &trThread));
    return Breakpoints::GetExceptionInfo(trThread, exceptionInfo);
}

HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                std::vector<Breakpoint> &breakpoints)
{
    return Breakpoints::SetExceptionBreakpoints(exceptionBreakpoints, breakpoints);
}

HRESULT SetSourceBreakpoints(const Source &source,
                             const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return Breakpoints::SetSourceBreakpoints(haveProcess, source, sourceBreakpoints, breakpoints);
}

HRESULT SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints)
{
    const bool haveProcess = HaveDebugProcess();
    return Breakpoints::SetFunctionBreakpoints(haveProcess, functionBreakpoints, breakpoints);
}

HRESULT GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                      std::vector<StackFrame> &stackFrames)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    ToRelease<ICorDebugThread> trThread;
    if (SUCCEEDED(Status = GetTrProcess()->GetThread(static_cast<int>(threadId), &trThread)))
    {
        return GetStackFrames(trThread, threadId, startFrame, maxFrames, stackFrames);
    }

    return Status;
}

HRESULT GetVariables(uint32_t variablesReference, std::vector<Variable> &variables)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return Variables::GetVariables(GetTrProcess(), variablesReference, variables);
}

HRESULT GetScopes(FrameId frameId, std::vector<Scope> &scopes)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return Variables::GetScopes(GetTrProcess(), frameId, scopes);
}

HRESULT Evaluate(FrameId frameId, const std::string &expression, Variable &variable, std::string &output)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return Variables::Evaluate(GetTrProcess(), frameId, expression, variable, output);
}

void CancelEvalRunning()
{
    EvalWaiter::CancelEvalRunning();
}

HRESULT SetVariable(const std::string &name, const std::string &value, uint32_t ref,
                    std::string &output)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return Variables::SetVariable(GetTrProcess(), name, value, ref, output);
}

HRESULT SetExpression(FrameId frameId, const std::string &expression,
                      const std::string &value, std::string &output)
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    return Variables::SetExpression(GetTrProcess(), frameId, expression, value, output);
}

void WriteStdin(gsl::span<const char> text)
{
    GetIORedirect().WriteStdin(text);
}

bool InitializeRemoteConsoleServer(int port)
{
    return GetRemoteConsoleServer().Initialize(port,
        [](gsl::span<char> text)
        {
            GetIORedirect().WriteStdin(text);
        });
}

void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules)
{
    Modules::GetModules(startModule, moduleCount, modules, totalModules);
}

HRESULT GetGotoTarget(const Source &source, int32_t line, int32_t column, std::vector<GotoTarget> &targets, std::string &output)
{
    HRESULT Status = S_OK;

    std::vector<GotoTarget> publicTargets;
    GetIntTargets().clear();

    IfFailRet(DebugInfo::GetGotoTarget(source, line, column, publicTargets, GetIntTargets(), output));

    targets = std::move(publicTargets);

    return S_OK;
}

HRESULT Goto(ThreadId threadId, uint32_t targetId, std::string &output)
{
    if (GetIntTargets().empty())
    {
        return E_INVALIDARG;
    }

    bool targetFound = false;
    uint32_t targetIndex = 0;
    for (const auto &target : GetIntTargets())
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

    const ReadLock r_lock(GetDebugProcessRWLock());
    HRESULT Status = S_OK;
    IfFailRet(CheckDebugProcess());

    if (EvalWaiter::IsEvalRunning())
    {
        // Important! Abort all evals before 'Goto' in protocol: during an eval, the thread state is inconsistent.
        LOGE(log << "Can't 'Goto' during running evaluation.");
        return E_UNEXPECTED;
    }

    if (CallbacksQueue::IsRunning())
    {
        LOGI(log << "Can't 'Goto', process already running.");
        return E_FAIL;
    }

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(GetTrProcess()->GetThread(static_cast<int>(threadId), &trThread));
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

    const GotoTargetInternal &target = GetIntTargets().at(targetIndex);

    if (target.modAddress != modAddress ||
        target.methodToken != methodToken)
    {
        output = "Error setting next statement. The next statement cannot be set to another function.";
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    IfFailRet(trILFrame->SetIP(target.ilOffset));

    Variables::Cleanup();
    FrameId::invalidate();               // Clear all frames created during the break.

    Threads::SetLastStoppedThread(GetTrProcess(), threadId);

    return S_OK;
}

HRESULT GetSourceContent(const Source &source, std::string &sourceContent)
{
    return DebugInfo::GetSourceContent(source, sourceContent);
}

void GetLoadedSources(std::vector<Source> &sources)
{
    DebugInfo::GetLoadedSources(sources);
}

HRESULT GetBreakpointLocations(const Source &source, const BreakpointLocation &rangeToSearch,
                               std::vector<BreakpointLocation> &locations)
{
    return DebugInfo::GetBreakpointLocations(source, rangeToSearch, locations);
}

} // namespace dncdbg::ManagedDebugger
