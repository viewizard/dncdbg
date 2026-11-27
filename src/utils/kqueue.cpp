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
#include <mutex>
#include <sys/event.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace dncdbg::MacKqueue
{

namespace
{

int &kq()
{
    static int kq = -1;
    return kq;
}

// Written by the watcher worker and read by GetExitCode(), possibly from different threads,
// hence it is atomic. Only atomicity is needed here, as the interlock already provides ordering.
std::atomic<int> &exitCode()
{
    static std::atomic<int> exitCode{0}; // Same behavior as CoreCLR: by default, exit code is 0
    return exitCode;
}

// Serializes access to the kqueue descriptor and the watcher state between the debugger
// thread, the watcher worker and the cleanup path.
std::mutex &GetInterlock()
{
    static std::mutex interlock;
    return interlock;
}

// Interpret the kevent NOTE_EXITSTATUS data (a waitpid()-style status) and update the exit code.
void UpdateExitCodeFromStatus(int status)
{
    if (WIFEXITED(status))
    {
        exitCode().store(WEXITSTATUS(status), std::memory_order_relaxed);
    }
    else if (WIFSIGNALED(status))
    {
        LOGW(log << "Process terminated by signal " << WTERMSIG(status) << ". Assuming EXIT_FAILURE.");
        exitCode().store(EXIT_FAILURE, std::memory_order_relaxed);
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

    for (;;)
    {
        // The interlock is held across kevent(), so the descriptor cannot be closed
        // underneath the wait (see GetExitCode() and Cleanup()).
        const std::scoped_lock lock(GetInterlock());
        if (GetStopRequested().load(std::memory_order_acquire))
        {
            break;
        }

        const int fd = kq();
        if (fd == -1)
        {
            break;
        }

        struct kevent event{};
        const int nev = kevent(fd, nullptr, 0, &event, 1, &timeout);

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

        if (event.filter != EVFILT_PROC || ((event.fflags & NOTE_EXIT) == 0U))
        {
            continue; // Unexpected filter for this registration; keep waiting.
        }

        UpdateExitCodeFromStatus(static_cast<int>(event.data));
        close(fd);
        kq() = -1;
        break;
    }

    // Release the descriptor if the loop was interrupted before the process exit event.
    {
        const std::scoped_lock lock(GetInterlock());
        if (kq() != -1)
        {
            close(kq());
            kq() = -1;
        }
    }

    // If the debug session has not been cleaned up in the meantime, report the process exit to the caller.
    std::function<void(int)> exitProcess;
    int code = 0;
    {
        const std::scoped_lock lock(GetInterlock());
        // Re-check under the interlock: Cleanup() sets the flag before joining this worker.
        if (GetStopRequested().load(std::memory_order_acquire))
        {
            return;
        }
        // Move the callback out instead of copying it, as it is invoked only once.
        exitProcess = std::exchange(GetExitProcessCallback(), nullptr);
        code = exitCode().load(std::memory_order_relaxed);
    }

    // Invoke the callback without holding the interlock, so it can safely call back into this module.
    if (exitProcess)
    {
        exitProcess(code);
    }
}

} // unnamed namespace

bool SetupTrackingPID(pid_t pid)
{
    const std::scoped_lock lock(GetInterlock());

    // Do not leak a descriptor left from a previous session.
    if (kq() != -1)
    {
        close(kq());
        kq() = -1;
    }

    exitCode().store(0, std::memory_order_relaxed); // Same behavior as CoreCLR: by default, exit code is 0

    const int newKq = kqueue();
    if (newKq == -1)
    {
        LOGE(log << "Failed to create kqueue: " << strerror(errno));
        return false;
    }
    kq() = newKq;

    struct kevent change{};
    EV_SET(&change, pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT | NOTE_EXITSTATUS, 0, nullptr);
    if (kevent(newKq, &change, 1, nullptr, 0, nullptr) == -1)
    {
        LOGE(log << "Failed to register kevent for PID " << pid << ": " << strerror(errno));
        close(newKq);
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
    {
        const std::scoped_lock lock(GetInterlock());
        GetExitProcessCallback() = std::move(exitProcess);
        GetStopRequested().store(false, std::memory_order_release);
    }

    std::thread &waiterWorker = GetWaiterWorker();
    if (waiterWorker.joinable())
    {
        waiterWorker.join(); // Defensive: never assign over a joinable thread.
    }
    waiterWorker = std::thread{WaiterWorker};
}

int GetExitCode()
{
    // Hold the interlock across the blocking kevent(), so the descriptor cannot be
    // closed by the watcher worker or the cleanup path while it is in use.
    const std::scoped_lock lock(GetInterlock());

    const int fd = kq();
    if (fd == -1)
    {
        return exitCode().load(std::memory_order_relaxed);
    }

    // Note: This function is triggered by ManagedCallback::ExitProcess() after the
    // child process has already exited. We use a blocking kevent() call with a
    // 3-second timeout as a fallback mechanism, in case the kevent registration
    // in kqueue is still lagging.
    constexpr long timeoutSeconds = 3;
    struct timespec timeout{};
    timeout.tv_sec = timeoutSeconds;

    struct kevent event{};
    const int nev = kevent(fd, nullptr, 0, &event, 1, &timeout);
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

    close(fd);
    kq() = -1;

    return exitCode().load(std::memory_order_relaxed);
}

void Cleanup()
{
    // Signal the worker (if any) before joining, so it does not report the process exit.
    GetStopRequested().store(true, std::memory_order_release);
    if (GetWaiterWorker().joinable())
    {
        GetWaiterWorker().join();
    }

    const std::scoped_lock lock(GetInterlock());
    if (kq() != -1)
    {
        close(kq());
        kq() = -1;
    }

    exitCode().store(0, std::memory_order_relaxed); // Same behavior as CoreCLR: by default, exit code is 0
    GetExitProcessCallback() = {};
}

} // namespace dncdbg::MacKqueue

#endif // (defined(__APPLE__) && defined(__MACH__))
