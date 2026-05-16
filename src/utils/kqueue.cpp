// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if (defined(__APPLE__) && defined(__MACH__))

#include "utils/kqueue.h"
#include "utils/logger.h"
#include <cstdlib>
#include <cstring>
#include <sys/event.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dncdbg
{

int MacKqueue::kq = -1;
int MacKqueue::exitCode = 0; // Same behavior as CoreCLR: by default, exit code is 0

void MacKqueue::SetupTrackingPID(pid_t PID)
{
    kq = kqueue();
    exitCode = 0; // Same behavior as CoreCLR: by default, exit code is 0
    if (kq == -1)
    {
        LOGE(log << "Failed to create kqueue: " << strerror(errno));
        return;
    }
    struct kevent change{};
    EV_SET(&change, PID, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT | NOTE_EXITSTATUS, 0, nullptr);
    if (kevent(kq, &change, 1, nullptr, 0, nullptr) == -1)
    {
        LOGE(log << "Failed to register kevent for PID " << PID << ": " << strerror(errno));
    }
}

int MacKqueue::GetExitCode()
{
    if (kq == -1)
    {
        return exitCode;
    }

    // Note: This function is triggered by ManagedCallback::ExitProcess() after the
    // child process has already exited. We use a blocking kevent() call with a
    // 3-second timeout as a fallback mechanism, in case the kevent registration
    // in kqueue is still lagging.
    constexpr long timeoutSeconds = 3;
    struct timespec timeout;
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_nsec = 0;

    struct kevent event{};
    const int nev = kevent(kq, nullptr, 0, &event, 1, &timeout);
    if (nev > 0 && event.filter == EVFILT_PROC && ((event.fflags & NOTE_EXIT) != 0U))
    {
        const int status = static_cast<int>(event.data);

        if (WIFEXITED(status))
        {
            exitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            LOGW(log << "Process terminated by signal " << WTERMSIG(status) << ". Assuming EXIT_FAILURE.");
            exitCode = EXIT_FAILURE;
        }
    }
    else if (nev == 0)
    {
        LOGE(log << "kevent() timeout.");
    }
    else if (nev == -1)
    {
        LOGE(log << "kevent() failed: " << strerror(errno));
    }

    close(kq);
    kq = -1;

    return exitCode;
}

} // namespace dncdbg

#endif // (defined(__APPLE__) && defined(__MACH__))
