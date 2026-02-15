// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#pragma once

#include <string_view>

namespace BuildInfo
{

extern const std::string_view version;         // version for displaying
extern const std::string_view build_type;      // build type (same version might have different build types)

extern const std::string_view dncdbg_vcs_info; // GIT revision hash for dncdbg itself

extern const std::string_view os_name;         // OS name for which project was build.
extern const std::string_view cpu_arch;        // CPU architecture name for which project was build.

extern const std::string_view date;
extern const std::string_view time;            // Date and time of the build.

} // namespace BuildInfo
