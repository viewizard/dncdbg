// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_KQUEUE_H
#define UTILS_KQUEUE_H

#if (defined(__APPLE__) && defined(__MACH__))

#include <sys/types.h>

namespace dncdbg
{

class MacKqueue
{
  public:

    MacKqueue() = default;
    MacKqueue(MacKqueue &&) = delete;
    MacKqueue(const MacKqueue &) = delete;
    MacKqueue &operator=(MacKqueue &&) = delete;
    MacKqueue &operator=(const MacKqueue &) = delete;
    ~MacKqueue() = default;

    static void SetupTrackingPID(pid_t PID);
    static int GetExitCode();

  private:

    static int kq;
};

} // namespace dncdbg

#endif // (defined(__APPLE__) && defined(__MACH__))

#endif // UTILS_KQUEUE_H
