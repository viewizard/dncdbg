// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if (defined(__APPLE__) && defined(__MACH__))

#include "utils/kqueue.h"
#include "utils/logger.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sys/event.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace dncdbg::MacKqueue
{

namespace
{

int &kq()
{
    static int kq = -1;
    return kq;
}

int &exitCode()
{
    static int exitCode = 0; // Same behavior as CoreCLR: by default, exit code is 0
    return exitCode;
}

// Interpret the kevent NOTE_EXITSTATUS data (a waitpid()-style status) and update the exit code.
void UpdateExitCodeFromStatus(int status)
{
    if (WIFEXITED(status))
    {
        exitCode() = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        LOGW(log << "Process terminated by signal " << WTERMSIG(status) << ". Assuming EXIT_FAILURE.");
        exitCode() = EXIT_FAILURE;
    }
}

std::atomic<bool> &GetStopRequested()
{
    static std::atomic<bool> stopRequested{false};
    return stopRequested;
}

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

void WaiterWorker()
{
    constexpr long pollIntervalNs = 500L * 1000L * 1000L; // 500 ms
    struct timespec timeout{};
    timeout.tv_nsec = pollIntervalNs;

    while (!GetStopRequested().load())
    {
        struct kevent event{};
        const int nev = kevent(kq(), nullptr, 0, &event, 1, &timeout);

        if (nev == 0)
        {
            continue; // Timeout: the process is still running; re-check the stop flag.
        }

        if (nev == -1)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted by a signal; retry the wait.
            }
            LOGE(log << "kevent() failed: " << strerror(errno));
            break;
        }

        if (event.filter == EVFILT_PROC && ((event.fflags & NOTE_EXIT) != 0U))
        {
            UpdateExitCodeFromStatus(static_cast<int>(event.data));
            break;
        }

        // Unexpected filter for this registration; keep waiting.
    }

    close(kq());
    kq() = -1;

    // If the debug session has not been cleaned up in the meantime, report the process exit to the caller.
    if (!GetStopRequested().load())
    {
        const std::function<void(int)> &exitProcess = GetExitProcessCallback();
        if (exitProcess)
        {
            exitProcess(exitCode());
        }
    }
}

} // unnamed namespace

bool SetupTrackingPID(pid_t pid)
{
    kq() = kqueue();
    exitCode() = 0; // Same behavior as CoreCLR: by default, exit code is 0
    if (kq() == -1)
    {
        LOGE(log << "Failed to create kqueue: " << strerror(errno));
        return false;
    }
    struct kevent change{};
    EV_SET(&change, pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT | NOTE_EXITSTATUS, 0, nullptr);
    if (kevent(kq(), &change, 1, nullptr, 0, nullptr) == -1)
    {
        LOGE(log << "Failed to register kevent for PID " << pid << ": " << strerror(errno));
        close(kq());
        kq() = -1;
        return false;
    }

    return true;
}

void SetupTrackingHook(pid_t pid, std::function<void(int)> exitProcess)
{
    Cleanup(); // Stop the previous session watcher, if any.

    if (!SetupTrackingPID(pid))
    {
        // Tracking is unavailable (see SetupTrackingPID() log), so the process exit cannot
        // be detected and the callback cannot be invoked.
        return;
    }

    // NoDebug mode: no ICorDebug callbacks are delivered, hence nobody calls
    // GetExitCode() through ManagedCallback::ExitProcess(). Watch the process in a
    // worker thread and report the exit code once the process exits.
    GetExitProcessCallback() = std::move(exitProcess);
    GetStopRequested() = false;
    GetWaiterWorker() = std::thread{WaiterWorker};
}

int GetExitCode()
{
    if (kq() == -1)
    {
        return exitCode();
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
    const int nev = kevent(kq(), nullptr, 0, &event, 1, &timeout);
    if (nev > 0 && event.filter == EVFILT_PROC && ((event.fflags & NOTE_EXIT) != 0U))
    {
        UpdateExitCodeFromStatus(static_cast<int>(event.data));
    }
    else if (nev == 0)
    {
        LOGE(log << "kevent() timeout.");
    }
    else if (nev == -1)
    {
        LOGE(log << "kevent() failed: " << strerror(errno));
    }

    close(kq());
    kq() = -1;

    return exitCode();
}

void Cleanup()
{
    if (GetWaiterWorker().joinable())
    {
        GetStopRequested() = true;
        GetWaiterWorker().join();
    }

    if (kq() != -1)
    {
        close(kq());
        kq() = -1;
    }

    exitCode() = 0; // Same behavior as CoreCLR: by default, exit code is 0
    GetExitProcessCallback() = {};
}

} // namespace dncdbg::MacKqueue

#endif // (defined(__APPLE__) && defined(__MACH__))
