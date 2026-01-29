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
//   * DNCDBG_VCS_INFO, CORECLR_VCS_INFO -- both should contain GIT revision hash,
//     tag name, SVN revision number, etc... might be empty, if revision isn't known;
//
//   * OS_NAME should constain OS name for which project was build;
//
//   * CPU_ARCH should contain name of the CPU architecture;
//
// All macros listed above must not have enclosing double quotes and typically
// should be provided by buildsystem (CMake, etc...)

#include "buildinfo.h"

#define STRINGIFY_(v) #v
#define STRINGIFY(v) STRINGIFY_(v)

namespace dncdbg
{

namespace BuildInfo
{

const char version[] = "0.0.1";
const char build_type[] = STRINGIFY(BUILD_TYPE);

const char dncdbg_vcs_info[] = STRINGIFY(DNCDBG_VCS_INFO);
const char coreclr_vcs_info[]    = STRINGIFY(CORECLR_VCS_INFO);

const char os_name[]  = STRINGIFY(OS_NAME);
const char cpu_arch[] = STRINGIFY(CPU_ARCH);

const char date[]     = __DATE__;
const char time[]     = __TIME__;

} // namespace BuildInfo

} // namespace dncdbg
