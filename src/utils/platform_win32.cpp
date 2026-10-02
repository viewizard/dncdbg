// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/platform.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <cstdlib> // char **environ, EXIT_FAILURE

namespace dncdbg
{

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * static_cast<long>(usec)); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(nullptr, TRUE, nullptr);
    SetWaitableTimer(timer, &ft, 0, nullptr, nullptr, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

// Function returns list of environment variables (like char **environ).
char **GetSystemEnvironment()
{
    return environ;
}

void TerminateChildProcess(DWORD pid)
{
    // TerminateProcess() normally completes within a few milliseconds; the timeout is only
    // a safety net for a process stuck in the kernel (e.g., due to unfinished I/O), see
    // WaitForSingleObject() below.
    constexpr DWORD terminateWaitTimeoutMs = 500;

    // PROCESS_TERMINATE to terminate the process, SYNCHRONIZE to wait for its termination.
    HANDLE processHandle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (processHandle == nullptr)
    {
        const DWORD error = GetLastError();
        // The process may have already exited on its own (ERROR_INVALID_PARAMETER); the exit
        // is detected and reported by WinExit (see SetupTrackingHook()).
        if (error != ERROR_INVALID_PARAMETER)
        {
            LOGE(log << "OpenProcess failed for PID " << pid << ", error=" << error);
        }
        return;
    }

    if (TerminateProcess(processHandle, EXIT_FAILURE) == FALSE)
    {
        const DWORD error = GetLastError();
        LOGE(log << "TerminateProcess failed for PID " << pid << ", error=" << error);
        CloseHandle(processHandle);
        return;
    }

    // TerminateProcess() only requests termination; it returns immediately. Wait until the
    // process has actually terminated. The process exit itself is detected and reported by
    // WinExit (see SetupTrackingHook()).
    const DWORD waitResult = WaitForSingleObject(processHandle, terminateWaitTimeoutMs);
    if (waitResult != WAIT_OBJECT_0)
    {
        if (waitResult == WAIT_FAILED)
        {
            const DWORD error = GetLastError();
            LOGE(log << "WaitForSingleObject failed for PID " << pid << ", error=" << error);
        }
        else
        {
            // WAIT_TIMEOUT: the process has not terminated yet; its exit will be reported
            // by WinExit.
            LOGW(log << "WaitForSingleObject timed out for PID " << pid << ".");
        }
    }

    CloseHandle(processHandle);
}

} // namespace dncdbg

#endif // _WIN32
