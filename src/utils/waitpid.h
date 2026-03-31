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
namespace hook
{

class waitpid_t
{
  public:

    waitpid_t() = default;
    waitpid_t(waitpid_t &&) = delete;
    waitpid_t(const waitpid_t &) = delete;
    waitpid_t &operator=(waitpid_t &&) = delete;
    waitpid_t &operator=(const waitpid_t &) = delete;
    ~waitpid_t() = default;

    pid_t operator()(pid_t pid, int *status, int options);
    void SetupTrackingPID(pid_t PID);
    int GetExitCode();
    void SetExitCode(pid_t PID, int Code);

  private:

    using Signature = pid_t (*)(pid_t pid, int *status, int options);
    Signature original = nullptr;
    static constexpr pid_t notConfigured = -1;
    pid_t trackPID = notConfigured;
    int exitCode = 0; // same behaviour as CoreCLR has, by default exit code is 0
    std::recursive_mutex interlock;

    void init() noexcept;
};

} // namespace hook

hook::waitpid_t &GetWaitpid();

} // namespace dncdbg

#endif // FEATURE_PAL

#endif // UTILS_WAITPID_H
