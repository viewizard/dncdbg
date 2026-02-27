// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.
//
// Note: this file should be compiled with following C-preprocessor macros defined:
//
//   * VERSION -- version for displaing (like x.y.z, short string);
//
//   * BUILD_TYPE -- Debug, Release...
//
//   * DNCDBG_VCS_INFO -- should contain GIT revision hash,
//     tag name, SVN revision number, etc... might be empty, if revision isn't known;
//
//   * OS_NAME should constain OS name for which project was build;
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

const std::string_view version = "0.0.1";
const std::string_view build_type = STRINGIFY(BUILD_TYPE);

const std::string_view dncdbg_vcs_info = STRINGIFY(DNCDBG_VCS_INFO);

const std::string_view os_name = STRINGIFY(OS_NAME);
const std::string_view cpu_arch = STRINGIFY(CPU_ARCH);

const std::string_view date = __DATE__;
const std::string_view time = __TIME__;

} // namespace dncdbg::BuildInfo
