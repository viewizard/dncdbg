// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/winexit.h"
#include "utils/logger.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace dncdbg::WinExit
{

namespace
{

HANDLE &processHandle()
{
    static HANDLE processHandle = nullptr;
    return processHandle;
}

HANDLE &waitHandle()
{
    static HANDLE waitHandle = nullptr;
    return waitHandle;
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

// Exit process callback for the current session, see SetupTrackingHook().
std::function<void(int)> &GetExitProcessCallback()
{
    static std::function<void(int)> exitProcessCallback;
    return exitProcessCallback;
}

// Thread pool callback for the registered wait, see RegisterWaitForSingleObject().
// The wait is registered with WT_EXECUTEONLYONCE, since a terminated process remains
// in the signaled state (otherwise the callback could be invoked repeatedly), and with
// the INFINITE timeout, so the callback is invoked only when the process actually exits.
// The callback is queued on a thread pool worker thread, hence no dedicated watcher
// thread is required.
void CALLBACK ProcessExitCallback([[maybe_unused]] PVOID lpParameter, BOOLEAN timerOrWaitFired)
{
    std::unique_lock<std::recursive_mutex> mutex_guard(GetInterlock());

    // If the debug session has been cleaned up in the meantime, do not report the exit.
    if (GetStopRequested().load())
    {
        return;
    }

    if (timerOrWaitFired != FALSE)
    {
        // Cannot happen with the INFINITE timeout; do not report the exit without a valid exit code.
        LOGW(log << "Process exit wait timed out.");
        return;
    }

    GetStopRequested() = true; // The process exit is reported only once.

    // The registered wait guarantees the process has terminated, so the exit code is final
    // here and STILL_ACTIVE cannot be returned.
    DWORD dwExitCode = 0;
    int exitCode = 0;
    if (GetExitCodeProcess(processHandle(), &dwExitCode) != FALSE)
    {
        // Windows exit codes are full 32-bit values and can exceed INT_MAX (e.g., exception
        // codes such as 0xC0000005); pass them through unchanged, cast to a signed int.
        exitCode = static_cast<int>(dwExitCode);
    }
    else
    {
        const DWORD error = GetLastError();
        LOGE(log << "GetExitCodeProcess failed, error=" << error);
    }

    // Do not hold the interlock while the callback runs, so the debug session cleanup
    // (see Cleanup()) is not blocked and cannot deadlock against this thread pool thread.
    const std::function<void(int)> exitProcess = GetExitProcessCallback();
    mutex_guard.unlock();

    if (exitProcess)
    {
        exitProcess(exitCode);
    }
}

} // unnamed namespace

void SetupTrackingHook(DWORD pid, std::function<void(int)> exitProcess)
{
    Cleanup(); // Stop the previous session watcher, if any.

    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());

    // SYNCHRONIZE to wait on the process handle and PROCESS_QUERY_LIMITED_INFORMATION
    // to query the exit code (see GetExitCodeProcess()).
    processHandle() = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (processHandle() == nullptr)
    {
        const DWORD error = GetLastError();
        LOGE(log << "OpenProcess failed for PID " << pid << ", error=" << error);
        // Tracking is unavailable (see log), so the process exit cannot be detected and
        // the callback cannot be invoked.
        return;
    }

    GetExitProcessCallback() = std::move(exitProcess);
    GetStopRequested() = false;

    if (RegisterWaitForSingleObject(&waitHandle(), processHandle(), ProcessExitCallback, nullptr, INFINITE, WT_EXECUTEONLYONCE) == FALSE)
    {
        const DWORD error = GetLastError();
        LOGE(log << "RegisterWaitForSingleObject failed for PID " << pid << ", error=" << error);
        CloseHandle(processHandle());
        processHandle() = nullptr;
        waitHandle() = nullptr;
        GetExitProcessCallback() = {};
        GetStopRequested() = true;
    }
}

void Cleanup()
{
    HANDLE waitHandleCopy = nullptr;
    bool waitCanceled = false;

    {
        const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
        waitHandleCopy = waitHandle();
        waitHandle() = nullptr;
        GetStopRequested() = true; // The session is over, do not report the process exit anymore.
    }

    if (waitHandleCopy != nullptr)
    {
        // Note, even wait operations registered with WT_EXECUTEONLYONCE must be canceled. The
        // blocking form (INVALID_HANDLE_VALUE) waits until all in-flight ProcessExitCallback()
        // calls complete, so it becomes safe to close the process handle below (per the
        // RegisterWaitForSingleObject() documentation, closing the waited-on handle while the
        // wait is still pending is undefined). Note, do not hold the interlock during this
        // call, otherwise an in-flight ProcessExitCallback() blocked on the interlock would
        // deadlock.
        if (UnregisterWaitEx(waitHandleCopy, INVALID_HANDLE_VALUE) != FALSE)
        {
            waitCanceled = true;
        }
        else
        {
            const DWORD error = GetLastError();
            LOGE(log << "UnregisterWaitEx failed, error=" << error);
            // Keep the process handle open: closing the waited-on handle of a still-pending
            // wait is undefined (see RegisterWaitForSingleObject()).
        }
    }

    const std::scoped_lock<std::recursive_mutex> mutex_guard(GetInterlock());
    if (waitCanceled && processHandle() != nullptr)
    {
        CloseHandle(processHandle());
        processHandle() = nullptr;
    }

    GetExitProcessCallback() = {};
}

} // namespace dncdbg::WinExit

#endif // _WIN32
