// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_KQUEUE_H
#define UTILS_KQUEUE_H

#if (defined(__APPLE__) && defined(__MACH__))

#include <sys/types.h>

namespace dncdbg::MacKqueue
{

void SetupTrackingPID(pid_t PID);
int GetExitCode();

} // namespace dncdbg::MacKqueue

#endif // (defined(__APPLE__) && defined(__MACH__))

#endif // UTILS_KQUEUE_H
