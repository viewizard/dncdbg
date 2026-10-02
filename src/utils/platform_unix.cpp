// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#if (defined(__APPLE__) && defined(__MACH__))
#include <crt_externs.h>
#endif

#include "utils/platform.h"
#include "utils/logger.h"
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstring>

//extern char **environ; // unistd.h should have this line

namespace dncdbg
{

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec)
{
    usleep(usec);
}

// Function returns list of environment variables (like char **environ).
char **GetSystemEnvironment()
{
#if (defined(__APPLE__) && defined(__MACH__))
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

void TerminateChildProcess(pid_t pid)
{
    constexpr unsigned long terminateGracePeriodUsec = 500UL * 1000UL; // 500 ms

    if (kill(pid, SIGTERM) == -1)
    {
        // The process may have already exited on its own (errno == ESRCH); the process exit is
        // detected and reported by the platform-specific watcher (see WaitpidHook/MacKqueue).
        if (errno != ESRCH)
        {
            LOGE(log << "kill(SIGTERM) failed for PID " << pid << ", errno=" << errno << " (" << strerror(errno) << ")");
        }
        return;
    }

    // Give the process a chance to terminate on its own (e.g., to run its signal handlers
    // and flush the output) before escalating to SIGKILL.
    USleep(terminateGracePeriodUsec);

    // Note that kill() reports a not-yet-reaped zombie as an existing process, and SIGKILL has
    // no effect on a zombie, so the escalation is safe in either case. Process exit detection
    // and reporting is done by the platform-specific watcher (see WaitpidHook/MacKqueue),
    // hence the child process is deliberately not reaped here.
    if (kill(pid, 0) == 0 && kill(pid, SIGKILL) == -1 && errno != ESRCH)
    {
        LOGE(log << "kill(SIGKILL) failed for PID " << pid << ", errno=" << errno << " (" << strerror(errno) << ")");
    }
}

} // namespace dncdbg

#endif // FEATURE_PAL
