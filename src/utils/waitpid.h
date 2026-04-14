// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_WAITPID_H
#define UTILS_WAITPID_H

#ifdef FEATURE_PAL

#include <mutex>
#include <csignal>

namespace dncdbg
{

class WaitpidHook
{
  public:

    WaitpidHook() = default;
    WaitpidHook(WaitpidHook &&) = delete;
    WaitpidHook(const WaitpidHook &) = delete;
    WaitpidHook &operator=(WaitpidHook &&) = delete;
    WaitpidHook &operator=(const WaitpidHook &) = delete;
    ~WaitpidHook() = default;

    static pid_t CallOriginal(pid_t pid, int *status, int options);
    static void SetupTrackingPID(pid_t PID);
    static int GetExitCode();
    static void SetExitCode(pid_t PID, int Code);

  private:

    using Signature = pid_t (*)(pid_t pid, int *status, int options);

    static Signature original;
    static constexpr pid_t notConfigured = -1;
    static pid_t trackPID;
    static int exitCode;
    static std::recursive_mutex interlock;

    static void init() noexcept;
};

} // namespace dncdbg

#endif // FEATURE_PAL

#endif // UTILS_WAITPID_H
