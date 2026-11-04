// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef BUILDINFO_H
#define BUILDINFO_H

#include <string_view>

namespace BuildInfo
{

extern const std::string_view version;         // version for displaying
extern const std::string_view buildType;       // build type (same version might have different build types)

extern const std::string_view dncdbgVcsInfo;   // Git revision hash for dncdbg itself

extern const std::string_view osName;          // OS name for which the project was built.
extern const std::string_view cpuArch;         // CPU architecture name for which the project was built.

extern const std::string_view date;
extern const std::string_view time;            // Date and time of the build.

} // namespace BuildInfo

#endif // BUILDINFO_H
