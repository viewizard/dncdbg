// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_SOURCEFILEMAP_H
#define DEBUGINFO_SOURCEFILEMAP_H

#include <map>
#include <string>

namespace dncdbg
{

class SourceFileMap
{
  public:

    // Return source path with applied source file path mapping.
    static std::string Path(const std::string &path);

    static std::map<std::string, std::string> &GetMap()
    {
        static std::map<std::string, std::string> sourceFileMap;
        return sourceFileMap;
    }
};

} // namespace dncdbg

#endif // DEBUGINFO_SOURCEFILEMAP_H
