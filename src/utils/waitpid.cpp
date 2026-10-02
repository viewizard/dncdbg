// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef __linux__

#include "utils/waitpid.h"
#include "utils/logger.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <mutex>
#include <sys/wait.h>
#include <thread>

namespace dncdbg::WaitpidHook
{

namespace
{

using Signature = pid_t (*)(pid_t pid, int *status, int options);

constexpr pid_t notConfigured = -1;

Signature &original()
{
    static Signature original = nullptr;
    return original;
}

pid_t &trackPID()
{
    static pid_t trackPID = notConfigured;
    return trackPID;
}

int &exitCode()
{
    static int exitCode = 0; // Same behavior as CoreCLR: by default, exit code is 0
    return exitCode;
}

std::recursive_mutex &GetInterlock()
{
    static std::recursive_mutex interlock;
    return interlock;
}

std::atomic<bool> &GetStopRequested()
{
    static std::atomic<bool> stopRequested{false};
    return stopRequested;
}

void init() noexcept
{
    auto *ret = dlsym(RTLD_NEXT, "waitpid");
    if (ret == nullptr)
    {
        LOGE(log << "Could not find original function waitpid");
        abort();
    }
    original() = reinterpret_cast<Signature>(ret);
}

pid_t CallOriginal(pid_t pid, int *status, int options)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    if (original() == nullptr)
    {
        init();
    }
    return original()(pid, status, options);
}

// Exit process callback for the current session, see SetupTrackingHook().
std::function<void(int)> &GetExitProcessCallback()
{
    static std::function<void(int)> exitProcessCallback;
    return exitProcessCallback;
}

std::thread &GetWaiterWorker()
{
    static std::thread waiterWorker;
    return waiterWorker;
}

void SetExitCode(pid_t pid, int code)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    if (trackPID() == notConfigured || pid != trackPID())
    {
        return;
    }
    exitCode() = code;

    // NoDebug mode: the callback is provided by SetupTrackingHook() and must be invoked
    // when the process exits, since no ICorDebug callbacks are delivered in this mode.
    // If the debug session has been cleaned up in the meantime, do not report the exit.
    const std::function<void(int)> &exitProcess = GetExitProcessCallback();
    if (exitProcess && !GetStopRequested().load())
    {
        exitProcess(code);
    }
}

// Same logic as the PAL has, see PROCGetProcessStatus() and CPalSynchronizationManager::HasProcessExited()
void HandleProcessExit(pid_t pid, int status)
{
    if (WIFEXITED(status))
    {
        SetExitCode(pid, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        LOGW(log << "Process terminated without exiting, can't get exit code. Killed by signal " << WTERMSIG(status) << ". Assuming EXIT_FAILURE.");
        SetExitCode(pid, EXIT_FAILURE);
    }
}

void WaiterWorker(pid_t pid)
{
    constexpr auto pollInterval = std::chrono::milliseconds{500};

    while (!GetStopRequested().load())
    {
        int status = 0;
        const pid_t pidWaitRetval = CallOriginal(pid, &status, WNOHANG);

        if (pidWaitRetval == pid)
        {
            HandleProcessExit(pid, status);
            break; // Process exited and reported to the caller.
        }

        if (pidWaitRetval == -1)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted by a signal; retry the wait.
            }
            LOGE(log << "waitpid() failed: " << strerror(errno));
            break;
        }

        std::this_thread::sleep_for(pollInterval); // The process is still running; re-check the stop flag.
    }
}

} // namespace

void SetupTrackingPID(pid_t pid)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    trackPID() = pid;
    exitCode() = 0; // Same behavior as CoreCLR: by default, exit code is 0
}

int GetExitCode()
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    return exitCode();
}

void SetupTrackingHook(pid_t pid, std::function<void(int)> exitProcess)
{
    Cleanup(); // Stop the previous session watcher, if any.

    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    // NoDebug mode: no ICorDebug callbacks are delivered, hence nobody calls GetExitCode()
    // through ManagedCallback::ExitProcess(). Watch the process in a worker thread and
    // report the exit code once the process exits. The callback is set before tracking
    // starts, so a process exit cannot be missed.
    GetExitProcessCallback() = std::move(exitProcess);
    SetupTrackingPID(pid);

    GetStopRequested() = false;
    GetWaiterWorker() = std::thread{WaiterWorker, pid};
}

void Cleanup()
{
    if (GetWaiterWorker().joinable())
    {
        GetStopRequested() = true;
        GetWaiterWorker().join();
    }

    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    trackPID() = notConfigured;
    exitCode() = 0; // Same behavior as CoreCLR: by default, exit code is 0
    GetExitProcessCallback() = {};
}

// Note, we guarantee `waitpid()` hook works only during debuggee process execution;
// it is aimed to work only for PAL's `waitpid()` calls interception.
extern "C" pid_t waitpid(pid_t pid, int *status, int options) // NOLINT(readability-inconsistent-declaration-parameter-name)
{
    const pid_t pidWaitRetval = dncdbg::WaitpidHook::CallOriginal(pid, status, options);

    if (pidWaitRetval == pid)
    {
        dncdbg::WaitpidHook::HandleProcessExit(pid, *status);
    }

    return pidWaitRetval;
}

} // namespace dncdbg::WaitpidHook

#endif // __linux__
