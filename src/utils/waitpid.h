// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_WAITPID_H
#define UTILS_WAITPID_H

#ifdef __linux__

#include <sys/types.h>

namespace dncdbg::WaitpidHook
{

void SetupTrackingPID(pid_t PID);
int GetExitCode();

} // namespace dncdbg::WaitpidHook

#endif // __linux__

#endif // UTILS_WAITPID_H
