// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if (defined(__APPLE__) && defined(__MACH__))

#include "utils/kqueue.h"
#include "utils/logger.h"
#include <cstdlib>
#include <cstring>
#include <sys/event.h>
#include <sys/wait.h>

namespace dncdbg
{

int MacKqueue::kq = -1;

void MacKqueue::SetupTrackingPID(pid_t PID)
{
    kq = kqueue();
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
        LOGW(log << "kqueue not initialized, returning exit code 0");
        return 0;
    }

    struct kevent event{};
    const int nev = kevent(kq, nullptr, 0, &event, 1, nullptr);
    if (nev > 0 && event.filter == EVFILT_PROC && ((event.fflags & NOTE_EXIT) != 0U))
    {
        const int status = static_cast<int>(event.data);

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status))
        {
            LOGW(log << "Process terminated by signal " << WTERMSIG(status) << ". Assuming EXIT_FAILURE.");
            return EXIT_FAILURE;
        }
    }
    else if (nev == -1)
    {
        LOGE(log << "kevent() failed: " << strerror(errno));
    }

    return 0;
}

} // namespace dncdbg

#endif // (defined(__APPLE__) && defined(__MACH__))
