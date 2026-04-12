// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_PLATFORM_H
#define UTILS_PLATFORM_H

#ifdef _WIN32
#define PLATFORM_TAG Win32PlatformTag
#else
#define PLATFORM_TAG UnixPlatformTag
#endif

namespace dncdbg
{

struct Win32PlatformTag
{
}; // PlatformTag for Windows (see below)
struct UnixPlatformTag
{
}; // PlatformTag for Unix and MacOS.

// PlatformTag is the type, which determines platform, for which code is currently compiled.
// This tag might be used to select proper template specialization.
using PlatformTag = PLATFORM_TAG;

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec);

// Function returns list of environment variables (like char **environ).
char **GetSystemEnvironment();

} // namespace dncdbg

#endif // UTILS_PLATFORM_H
