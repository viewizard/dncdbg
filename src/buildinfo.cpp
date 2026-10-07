// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
//
// Note: this file should be compiled with the following C preprocessor macros defined:
//
//   * VERSION -- the version to display (a short string, like x.y.z);
//
//   * BUILD_TYPE -- Debug, Release, etc.;
//
//   * DNCDBG_VCS_INFO -- should contain the Git revision hash, tag name,
//     SVN revision number, etc.; it may be empty if the revision is unknown;
//
//   * OS_NAME -- should contain the name of the OS the project was built for;
//
//   * CPU_ARCH -- should contain the name of the CPU architecture;
//
// All macros listed above must not be enclosed in double quotes and should
// typically be provided by the build system (CMake, etc.)

#include "buildinfo.h"

#define STRINGIFY_(v) #v // NOLINT(cppcoreguidelines-macro-usage)
#define STRINGIFY(v) STRINGIFY_(v) // NOLINT(cppcoreguidelines-macro-usage)

namespace BuildInfo
{

const std::string_view version = "1.2.0";
const std::string_view build_type = STRINGIFY(BUILD_TYPE);

const std::string_view dncdbg_vcs_info = STRINGIFY(DNCDBG_VCS_INFO);

const std::string_view os_name = STRINGIFY(OS_NAME);
const std::string_view cpu_arch = STRINGIFY(CPU_ARCH);

const std::string_view date = __DATE__;
const std::string_view time = __TIME__;

} // namespace BuildInfo
