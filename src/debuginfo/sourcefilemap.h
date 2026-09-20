// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_SOURCEFILEMAP_H
#define DEBUGINFO_SOURCEFILEMAP_H

#include <map>
#include <string>

namespace dncdbg::SourceFileMap
{

// Return source path with applied source file path mapping.
std::string Path(const std::string &path);

std::map<std::string, std::string> &GetMap();

} // namespace dncdbg::SourceFileMap

#endif // DEBUGINFO_SOURCEFILEMAP_H
