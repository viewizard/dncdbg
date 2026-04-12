// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_PLATFORM_H
#define UTILS_PLATFORM_H

namespace dncdbg
{

// Function suspends process execution for specified amount of time (in microseconds)
void USleep(unsigned long usec);

// Function returns list of environment variables (like char **environ).
char **GetSystemEnvironment();

} // namespace dncdbg

#endif // UTILS_PLATFORM_H
