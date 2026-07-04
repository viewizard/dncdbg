// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
//
// Note: this file should be compiled with following C-preprocessor macros defined:
//
//   * VERSION -- version for displaying (like x.y.z, short string);
//
//   * BUILD_TYPE -- Debug, Release...
//
//   * DNCDBG_VCS_INFO -- should contain GIT revision hash,
//     tag name, SVN revision number, etc... might be empty, if revision isn't known;
//
//   * OS_NAME should contain OS name for which project was build;
//
//   * CPU_ARCH should contain name of the CPU architecture;
//
// All macros listed above must not have enclosing double quotes and typically
// should be provided by buildsystem (CMake, etc...)

#include "buildinfo.h"

#define STRINGIFY_(v) #v // NOLINT(cppcoreguidelines-macro-usage)
#define STRINGIFY(v) STRINGIFY_(v) // NOLINT(cppcoreguidelines-macro-usage)

namespace BuildInfo
{

constexpr std::string_view version = "1.1.0";
constexpr std::string_view build_type = STRINGIFY(BUILD_TYPE);

constexpr std::string_view dncdbg_vcs_info = STRINGIFY(DNCDBG_VCS_INFO);

constexpr std::string_view os_name = STRINGIFY(OS_NAME);
constexpr std::string_view cpu_arch = STRINGIFY(CPU_ARCH);

constexpr std::string_view date = __DATE__;
constexpr std::string_view time = __TIME__;

} // namespace BuildInfo
