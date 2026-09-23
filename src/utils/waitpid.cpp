// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef __linux__

#include "utils/waitpid.h"
#include "utils/logger.h"
#include <dlfcn.h>
#include <cstdlib>
#include <mutex>
#include <sys/wait.h>

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

void SetExitCode(pid_t PID, int Code)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    if (trackPID() == notConfigured || PID != trackPID())
    {
        return;
    }
    exitCode() = Code;
}

} // namespace

void SetupTrackingPID(pid_t PID)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    trackPID() = PID;
    exitCode() = 0; // Same behavior as CoreCLR: by default, exit code is 0
}

int GetExitCode()
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    return exitCode();
}

// Note, we guarantee `waitpid()` hook works only during debuggee process execution;
// it is aimed to work only for PAL's `waitpid()` calls interception.
extern "C" pid_t waitpid(pid_t pid, int *status, int options) // NOLINT(readability-inconsistent-declaration-parameter-name)
{
    const pid_t pidWaitRetval = dncdbg::WaitpidHook::CallOriginal(pid, status, options);

    // same logic as PAL has, see PROCGetProcessStatus() and CPalSynchronizationManager::HasProcessExited()
    if (pidWaitRetval == pid)
    {
        if (WIFEXITED(*status))
        {
            dncdbg::WaitpidHook::SetExitCode(pid, WEXITSTATUS(*status));
        }
        else if (WIFSIGNALED(*status))
        {
            LOGW(log << "Process terminated without exiting, can't get exit code. Killed by signal " << WTERMSIG(*status) << ". Assuming EXIT_FAILURE.");
            dncdbg::WaitpidHook::SetExitCode(pid, EXIT_FAILURE);
        }
    }

    return pidWaitRetval;
}

// Note, liblttng-ust may call `wait()` at CoreCLR global/static initialization at dlopen() (debugger managed part related).
extern "C" pid_t wait(int *status) // NOLINT(readability-inconsistent-declaration-parameter-name)
{
    return waitpid(-1, status, 0);
}

} // namespace dncdbg::WaitpidHook

#endif // __linux__
