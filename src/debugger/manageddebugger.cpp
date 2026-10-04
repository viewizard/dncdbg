// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "debugger/manageddebugger.h"
#include "config/config.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/callbacksqueue.h"
#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "debugger/evaluation/evaluation.h"
#include "debugger/frames.h"
#include "debugger/goto.h"
#include "debugger/managedcallback.h"
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "debugger/variables.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/sourcefilemap.h"
#include "metadata/modules.h"
#include "protocol/dap_events.h"
#include "utils/dbgshim.h"
#include "utils/diagnostic_client.h"
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
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>

#if (defined(__APPLE__) && defined(__MACH__))
#include "utils/kqueue.h"
#elif defined(__linux__)
#include "utils/waitpid.h"
#elif defined(_WIN32)
#include "utils/winexit.h"
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

constexpr auto terminateTimeout = std::chrono::milliseconds(3000);

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
    // Prevent diagnostic port suspend usage during debuggee process creation, since the diagnostic part suspends
    // the debuggee process at an early launch stage and causes a `configurationDone` command timeout.
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
    // The runtime may not be ready to accept diagnostic commands yet,
    // so retry the resume a few times before giving up.
    static constexpr unsigned long initialSleepTime = 50000UL; // 0.05 sec
    unsigned long sleepTime = initialSleepTime;
    static constexpr uint8_t retriesLimit = 5;
    uint8_t retriesLeft = retriesLimit;
    while (FAILED(DiagnosticClient::ResumeRuntime(processId)) && retriesLeft > 0)
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

bool &GetNoDebug()
{
    static bool noDebug{false};
    return noDebug;
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

std::vector<Goto::TargetInternal> &GetIntTargets()
{
    static std::vector<Goto::TargetInternal> intTargets;
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
    if (GetNoDebug())
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    if (GetTrProcess() == nullptr)
    {
        return E_FAIL;
    }

    // The process may have exited or been detached while the process object has not been
    // freed yet, in which case the object is no longer valid.
    const std::scoped_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (GetProcessAttachedState() == ProcessAttachedState::Unattached)
    {
        return E_FAIL;
    }

    return S_OK;
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

// Return true if the NoDebug process is still running. In NoDebug mode there is no
// ICorDebug process object, so the process attached state is the only indicator of an
// active debug session.
bool IsNoDebugProcessAttached()
{
    const std::scoped_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    return GetProcessAttachedState() == ProcessAttachedState::Attached;
}

// Return true if the process is attached for real debugging (an ICorDebug process object
// exists and is attached). Unlike HaveProcess(), a NoDebug process is not counted here.
bool HaveDebugProcess()
{
    const ReadLock r_lock(GetDebugProcessRWLock());
    return SUCCEEDED(CheckDebugProcess());
}

// Install the platform-specific process exit watcher (see SetupTrackingHook() in the
// platform-specific implementation). Used in NoDebug mode, where no ICorDebug callbacks
// are delivered, so the process exit cannot otherwise be reported.
void SetupProcessExitWatcher(DWORD pid, std::function<void(int)> exitProcess)
{
#if (defined(__APPLE__) && defined(__MACH__))
    MacKqueue::SetupTrackingHook(static_cast<pid_t>(pid), std::move(exitProcess));
#elif defined(__linux__)
    WaitpidHook::SetupTrackingHook(static_cast<pid_t>(pid), std::move(exitProcess));
#elif defined(_WIN32)
    WinExit::SetupTrackingHook(pid, std::move(exitProcess));
#else
    static_assert(false, "Unsupported platform");
#endif
}

// Stop the platform-specific process exit watcher, if any.
void CleanupProcessExitWatcher()
{
#if (defined(__APPLE__) && defined(__MACH__))
    MacKqueue::Cleanup();
#elif defined(__linux__)
    WaitpidHook::Cleanup();
#elif defined(_WIN32)
    WinExit::Cleanup();
#else
    static_assert(false, "Unsupported platform");
#endif
}

void InputCallback(IORedirect::StreamType type, gsl::span<char> text)
{
    DAP::EmitOutputEvent(OutputEvent(type == IORedirect::StreamType::Stderr ? OutputCategory::StdErr : OutputCategory::StdOut, {text.data(), text.size()}));
    GetRemoteConsoleServer().SendData(text);
}

HRESULT Startup(IUnknown *pUnknown)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebug> trDebug;
    IfFailRet(pUnknown->QueryInterface(IID_ICorDebug, reinterpret_cast<void **>(&trDebug)));

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

    const WriteLock w_lock(GetDebugProcessRWLock());

    GetTrProcess() = trProcess.Detach();
    GetTrDebug() = trDebug.Detach();

    return S_OK;
}

// Called by dbgshim RegisterForRuntimeStartup().
void StartupCallback(IUnknown *pUnknown, void */*parameter*/, HRESULT hr)
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

    GetStartupCallbackHR() = Startup(pUnknown);

    if (GetUnregisterToken() != nullptr)
    {
        GetDbgshim().GetUnregisterForRuntimeStartup()(GetUnregisterToken());
        GetUnregisterToken() = nullptr;
    }
}

HRESULT CheckNoProcess()
{
    const ReadLock r_lock(GetDebugProcessRWLock());

    if (GetNoDebug())
    {
        if (IsNoDebugProcessAttached())
        {
            return E_FAIL; // A NoDebug process is already running
        }

        return S_OK;
    }

    if (GetTrProcess() == nullptr)
    {
        return S_OK;
    }

    const std::scoped_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (GetProcessAttachedState() == ProcessAttachedState::Attached)
    {
        return E_FAIL; // Already attached
    }

    return S_OK;
}

HRESULT LaunchProcess(const std::string &fileExec, const std::vector<std::string> &execArgs)
{
    HRESULT Status = S_OK;

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

    if (GetNoDebug())
    {
        NotifyProcessCreated();

        // NoDebug mode: the process is launched without a debug session, so no ICorDebug
        // callbacks are delivered. The platform-specific watcher installed below (see
        // SetupTrackingHook()) watches the process and reports its exit instead.
        const auto exitProcess = [](int exitCode)
        {
            DAP::EmitExitedEvent(ExitedEvent(exitCode));
            NotifyProcessExited();
            DAP::EmitTerminatedEvent();
        };

        // Install the watcher while the process is still suspended, so a fast process exit
        // cannot be missed by the watcher, and the exit cannot be reported before the process
        // creation is notified above.
        SetupProcessExitWatcher(GetProcessId(), exitProcess);

        IfFailRet(GetDbgshim().GetResumeProcess()(resumeHandle));
        GetDbgshim().GetCloseResumeHandle()(resumeHandle);

        DAP::EmitProcessEvent(GetProcessId(), fileExec, GetStartMethod());

        return S_OK;
    }

#if (defined(__APPLE__) && defined(__MACH__))
    MacKqueue::SetupTrackingPID(static_cast<pid_t>(GetProcessId()));
#elif defined(__linux__)
    WaitpidHook::SetupTrackingPID(static_cast<pid_t>(GetProcessId()));
#endif

    IfFailRet(GetDbgshim().GetRegisterForRuntimeStartup()(GetProcessId(), StartupCallback, nullptr, &GetUnregisterToken()));

    // Resume the process so that StartupCallback can run.
    IfFailRet(GetDbgshim().GetResumeProcess()(resumeHandle));
    GetDbgshim().GetCloseResumeHandle()(resumeHandle);

    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, std::chrono::milliseconds(Config::GetStartupTimeoutMs()),
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

    // Reset the startup callback error from a previous attach attempt, if any.
    GetStartupCallbackHR() = S_OK;

    IfFailRet(GetDbgshim().GetRegisterForRuntimeStartup()(GetProcessId(), StartupCallback, nullptr, &GetUnregisterToken()));

    // Resume the runtime so that StartupCallback can run.
    ResumeRuntime(GetProcessId());

    DAP::EmitProcessEvent(GetProcessId(), "dotnet", GetStartMethod());

    std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
    if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, std::chrono::milliseconds(Config::GetStartupTimeoutMs()),
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

    CleanupDebugSession();
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
            // https://learn.microsoft.com/en-us/dotnet/core/unmanaged-api/debugging/icordebug/icordebugcontroller-terminate-method
            // If the process is stopped when Terminate is called, the process should be continued by using the
            // ICorDebugController::Continue method so that the debugger receives confirmation of the termination
            // through the ICorDebugManagedCallback::ExitProcess or ICorDebugManagedCallback::ExitAppDomain callback.
            GetTrProcess()->Continue(0);

            if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, terminateTimeout,
                                                 [] { return GetProcessAttachedState() == ProcessAttachedState::Unattached; }))
            {
                // The ICorDebugManagedCallback::ExitProcess callback did not arrive in time; since we
                // free the process object anyway, reset the process attached state (see the session
                // cleanup below).
                GetProcessAttachedState() = ProcessAttachedState::Unattached;
            }
            break;
        }

        LOGE(log << "Process terminate failed: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        GetProcessAttachedState() = ProcessAttachedState::Unattached; // Since we free process object anyway, reset process attached state.
    }
    while (false);

    CleanupDebugSession();
    return S_OK;
}

void ReleaseICorDebug()
{
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

    CleanupProcessExitWatcher();
}

HRESULT Attach(DWORD pid)
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    GetStartMethod() = StartMethod::Attach;
    Threads::SetProcessAttached(true);
    GetProcessId() = pid;
    // Reset launch-related options.
    GetExecPath().clear();
    GetNoDebug() = false;
    GetExecArgs().clear();
    GetCwd().clear();
    GetEnv().clear();
    return S_OK;
}

HRESULT Launch(const std::string &fileExec, bool noDebug, const std::vector<std::string> &execArgs,
               const std::map<std::string, std::string> &env, const std::string &cwd)
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    GetStartMethod() = StartMethod::Launch;
    Threads::SetProcessAttached(false);
    GetExecPath() = fileExec;
    GetNoDebug() = noDebug;
    GetExecArgs() = execArgs;
    GetCwd() = cwd;
    GetEnv() = env;
    // Reset attach-related options.
    GetProcessId() = 0;
    return S_OK;
}

void InitializeDebugSession()
{
    Breakpoints::Initialize();
    DebugInfo::Initialize();
    Goto::Initialize();

    Steppers::Cleanup();
    Variables::Cleanup();
    FrameId::Cleanup();
    Evaluation::Cleanup();
    Modules::Cleanup();
    Threads::Cleanup();
    CallbacksQueue::Cleanup();

    CleanupProcessExitWatcher();

    ReleaseICorDebug();
}

void CleanupDebugSession()
{
    Breakpoints::Cleanup();
    DebugInfo::Cleanup();
    Goto::Cleanup();

    Steppers::Cleanup();
    Variables::Cleanup();
    FrameId::Cleanup();
    Evaluation::Cleanup();
    Modules::Cleanup();
    Threads::Cleanup();
    CallbacksQueue::Cleanup();

    CleanupProcessExitWatcher();

    ReleaseICorDebug();
}

HRESULT StartDebugSession()
{
    HRESULT Status = S_OK;

    IfFailRet(CheckNoProcess());

    switch (GetStartMethod())
    {
    case StartMethod::Launch:
        return LaunchProcess(GetExecPath(), GetExecArgs());
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
        terminate = false;
        break;
    default:
        assert(false);
        return E_FAIL;
    }

    if (GetNoDebug())
    {
        // The process was launched without a debug session (see LaunchProcess()), so there is
        // no ICorDebug process object to detach from or terminate through. The process exit
        // is detected and reported by the platform-specific watcher (see SetupTrackingHook()).

        // The process is already gone (its exit has been reported by the watcher); there is
        // nothing to terminate or detach from, so just clean up the debug session.
        if (!IsNoDebugProcessAttached())
        {
            CleanupDebugSession();
            return S_OK;
        }

        if (terminate)
        {
#ifdef _WIN32
            TerminateChildProcess(static_cast<DWORD>(GetProcessId()));
#else
            TerminateChildProcess(static_cast<pid_t>(GetProcessId()));
#endif

            // Wait until the watcher reports the process exit (it also emits the `exited` and
            // `terminated` events), then clean up the debug session. If the watcher failed to
            // install or report the exit in time, stop it, so it cannot report the exit for
            // the already-ended session, and end the session anyway, so the client is not left
            // waiting for the `terminated` event.
            std::unique_lock<std::mutex> lockAttachedMutex(GetProcessAttachedMutex());
            if (!GetProcessAttachedCV().wait_for(lockAttachedMutex, terminateTimeout,
                                                 [] { return GetProcessAttachedState() == ProcessAttachedState::Unattached; }))
            {
                // Note, the lock must be released before the watcher cleanup, since an in-flight
                // watcher callback may need this mutex to report the process exit.
                lockAttachedMutex.unlock();
                CleanupProcessExitWatcher();
                DAP::EmitTerminatedEvent();
                NotifyProcessExited();
            }
        }
        else
        {
            // Keep the child process running on its own; the debug session just ends. Stop the
            // watcher first, so the child's exit cannot be reported for the already-ended session,
            // and reset the process attached state, so a new debug session can be started afterwards.
            CleanupProcessExitWatcher();
            DAP::EmitTerminatedEvent();
            NotifyProcessExited();
            GetIORedirect().Reset();
        }

        CleanupDebugSession();
        return S_OK;
    }

    if (!terminate)
    {
        const HRESULT Status = DetachFromProcess();
        if (SUCCEEDED(Status))
        {
            DAP::EmitTerminatedEvent();
            // The detached process keeps running on its own; stop forwarding its output.
            GetIORedirect().Reset();
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
        FrameId::Cleanup();                              // Clear all frames created during the break.
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
        FrameId::Cleanup();                              // Clear all frames created during the break.
        DAP::EmitContinuedEvent(threadId, singleThread); // DAP needs thread ID.
    }

    return Status;
}

bool HaveProcess()
{
    if (GetNoDebug())
    {
        return IsNoDebugProcessAttached();
    }

    return HaveDebugProcess();
}

bool IsProcessRunning()
{
    if (GetNoDebug())
    {
        return IsNoDebugProcessAttached();
    }

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
    return Breakpoints::SetSourceBreakpoints(HaveDebugProcess(), source, sourceBreakpoints, breakpoints);
}

HRESULT SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints)
{
    return Breakpoints::SetFunctionBreakpoints(HaveDebugProcess(), functionBreakpoints, breakpoints);
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

bool CloseRemoteConsoleServer()
{
    return GetRemoteConsoleServer().Close();
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

    IfFailRet(Goto::GetTarget(source, line, column, publicTargets, GetIntTargets(), output));

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

    const Goto::TargetInternal &target = GetIntTargets().at(targetIndex);

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
    FrameId::Cleanup(); // Clear all frames created during the break.

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

void SetSourceFileMap(std::map<std::string, std::string> &&map)
{
    SourceFileMap::SetSourceFileMap(std::move(map));
}

} // namespace dncdbg::ManagedDebugger
