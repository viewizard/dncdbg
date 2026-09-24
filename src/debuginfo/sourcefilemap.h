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

// Replace the source file path mapping with the given one.
void SetSourceFileMap(std::map<std::string, std::string> &&map);

} // namespace dncdbg::SourceFileMap

#endif // DEBUGINFO_SOURCEFILEMAP_H
