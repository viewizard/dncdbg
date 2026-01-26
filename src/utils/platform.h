// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#ifdef _MSC_VER
#define W(s) L##s
#else
#define W(s) u##s
#endif

#ifdef _WIN32
#define PLATFORM_TAG Win32PlatformTag
#else
#define PLATFORM_TAG UnixPlatformTag
#endif

namespace dncdbg
{

    struct Win32PlatformTag {};  // PlatformTag for Windows (see below)
    struct UnixPlatformTag {};   // PlatformTAg for Unix and MacOS.
   
    // PlatformTag is the type, which determines platform, for which code is currenly compilih.
    // This tag might be used to select proper template specialization.
    using PlatformTag = PLATFORM_TAG;

    // Function suspends process execution for specified amount of time (in microseconds)
    void USleep(unsigned long usec);

    // Function returns list of environment variables (like char **environ).
    char** GetSystemEnvironment();

} // namespace dncdbg
