// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#include "utils/platform.h"
#include "utils/utf.h"
#include <cstdlib> // char **environ
#include <windows.h>

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

// Function retrieves the value of an environment variable by name and returns it as UTF-8 string.
// Returns empty string if the environment variable is not found.
std::string GetEnvUtf8(const std::string &name)
{
    WSTRING wName = to_utf16(name);
    const WCHAR *wValue = _wgetenv(wName.c_str());
    if (wValue != nullptr)
    {
        return to_utf8(wValue);
    }

    return {};
}

} // namespace dncdbg
#endif
