// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#include "utils/platform.h"
#include "utils/limits.h"
#include <stdlib.h> // char **environ
#include <windows.h>

namespace dncdbg
{

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * (long)usec); // Convert to 100 nanosecond interval, negative value indicates relative time

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

} // namespace dncdbg
#endif
