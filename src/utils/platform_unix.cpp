// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#if (defined(__APPLE__) && defined(__MACH__))
#include <crt_externs.h>
#endif

#include "utils/platform.h"
#include <unistd.h>

//extern char **environ; // unistd.h should have this line

namespace dncdbg
{

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec)
{
    usleep(usec);
}

// Function returns list of environment variables (like char **environ).
char **GetSystemEnvironment()
{
#if (defined(__APPLE__) && defined(__MACH__))
    return *(_NSGetEnviron());
#else
    return environ;
#endif
}

} // namespace dncdbg

#endif // FEATURE_PAL
