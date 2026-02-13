// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/waitpid.h"
#include "utils/logger.h"
#include <dlfcn.h>
#include <cstdlib>
#include <sys/wait.h>

namespace dncdbg
{
namespace hook
{

void waitpid_t::init() noexcept
{
    auto *ret = dlsym(RTLD_NEXT, "waitpid");
    if (ret == nullptr)
    {
        LOGE("Could not find original function waitpid");
        abort();
    }
    original = reinterpret_cast<Signature>(ret);
}

pid_t waitpid_t::operator()(pid_t pid, int *status, int options)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(interlock);
    if (original == nullptr)
    {
        init();
    }
    return original(pid, status, options);
}

void waitpid_t::SetupTrackingPID(pid_t PID)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(interlock);
    trackPID = PID;
    exitCode = 0; // same behaviour as CoreCLR have, by default exit code is 0
}

int waitpid_t::GetExitCode()
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(interlock);
    return exitCode;
}

void waitpid_t::SetExitCode(pid_t PID, int Code)
{
    const std::scoped_lock<std::recursive_mutex> mutex_guard(interlock);
    if (trackPID == notConfigured || PID != trackPID)
    {
        return;
    }
    exitCode = Code;
}

static waitpid_t waitpid;

} // namespace hook

hook::waitpid_t &GetWaitpid()
{
    return hook::waitpid;
}

// Note, we guaranty `waitpid()` hook works only during debuggee process execution, it aimed to work only for PAL's `waitpid()` calls interception.
extern "C" pid_t waitpid(pid_t pid, int *status, int options) // NOLINT(readability-inconsistent-declaration-parameter-name)
{
    const pid_t pidWaitRetval = dncdbg::hook::waitpid(pid, status, options);

    // same logic as PAL have, see PROCGetProcessStatus() and CPalSynchronizationManager::HasProcessExited()
    if (pidWaitRetval == pid)
    {
        if (WIFEXITED(*status))
        {
            dncdbg::hook::waitpid.SetExitCode(pid, WEXITSTATUS(*status));
        }
        else if (WIFSIGNALED(*status))
        {
            LOGW("Process terminated without exiting, can't get exit code. Killed by signal %d. Assuming EXIT_FAILURE.", WTERMSIG(*status));
            dncdbg::hook::waitpid.SetExitCode(pid, EXIT_FAILURE);
        }
    }

    return pidWaitRetval;
}

// Note, liblttng-ust may call `wait()` at CoreCLR global/static initialization at dlopen() (debugger managed part related).
extern "C" pid_t wait(int *status) // NOLINT(readability-inconsistent-declaration-parameter-name)
{
    return waitpid(-1, status, 0);
}

} // namespace dncdbg

#endif // FEATURE_PAL
