// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_KQUEUE_H
#define UTILS_KQUEUE_H

#if (defined(__APPLE__) && defined(__MACH__))

#include <functional>
#include <sys/types.h>

namespace dncdbg::MacKqueue
{

bool SetupTrackingPID(pid_t pid);
void SetupTrackingHook(pid_t pid, std::function<void(int)> exitProcess);
int GetExitCode();
void Cleanup();

} // namespace dncdbg::MacKqueue

#endif // (defined(__APPLE__) && defined(__MACH__))

#endif // UTILS_KQUEUE_H
