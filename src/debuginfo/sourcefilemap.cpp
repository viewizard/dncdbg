// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcefilemap.h"
#include <algorithm>

namespace dncdbg
{

std::string SourceFileMap::Path(const std::string &path)
{
    if (GetMap().empty())
    {
        return path;
    }

    // https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map
    // It can either be a directory that has source files under it,
    // or a complete path to a source file (example: c:\foo\program.cs).

    size_t oldPrefixSize = 0;
    std::string newPrefix;

    for (const auto &[oldLocation, newLocation] : GetMap())
    {
        // Find the largest possible prefix from source file map (for example, `/folder` and `/folder/folder2`, the second must be used).
        if (oldLocation.size() <= oldPrefixSize ||
            // Check for prefix.
            path.size() < oldLocation.size() ||
            path.rfind(oldLocation, 0) != 0 || // Note: starts_with() is C++20, use rfind() for compatibility
            // In case this is part of path, not a file, delimiter must be the next symbol in path.
            (path.size() > oldLocation.size() && path.at(oldLocation.size()) != '/' && path.at(oldLocation.size()) != '\\'))
        {
            continue;
        }

        // File, just replace with new source file path.
        if (path.size() == oldLocation.size())
        {
            return newLocation;
        }

        oldPrefixSize = oldLocation.size();
        newPrefix = newLocation;
    }

    // Not in source file map.
    if (oldPrefixSize == 0)
    {
        return path;
    }

    // Find new delimiter and change delimiters to new one.
    const size_t posSlash = newPrefix.find('/');
    const size_t posBackslash = newPrefix.find('\\');
    char newDelimiter = '/';
    if ((posBackslash != std::string::npos && posSlash == std::string::npos) ||
        (posBackslash != std::string::npos && posSlash != std::string::npos && posBackslash < posSlash))
    {
        newDelimiter = '\\';
    }

    std::string endPath = path.substr(oldPrefixSize);
    std::replace(endPath.begin(), endPath.end(), newDelimiter == '/' ? '\\' : '/', newDelimiter);
    return newPrefix + endPath;
}

} // namespace dncdbg
