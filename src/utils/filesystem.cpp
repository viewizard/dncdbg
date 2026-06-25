// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/filesystem.h"

namespace dncdbg
{

// Function returns only file name part of the full path.
std::string GetBasename(const std::string &path)
{
    const size_t i = path.find_last_of(FileSystem::PathSeparatorSymbols);
    return i == std::string::npos ? path : path.substr(i + 1);
}

// Function checks, if given path contains directory names (strictly speaking,
// contains path separator) or consists only of a file name. Return value is `true`
// if argument is not the file name, but the path which includes directory names.
bool IsFullPath(const std::string &path)
{
    const size_t pos = path.find_last_of(FileSystem::PathSeparatorSymbols);
    return pos != std::string::npos;
}

std::string GetFileName(const std::string &path)
{
    const std::size_t i = path.find_last_of("/\\");
    return i == std::string::npos ? path : path.substr(i + 1);
}

std::string GetParentPath(const std::string &full_path)
{
    if (full_path.empty())
    {
        return {};
    }

    const size_t found = full_path.find_last_of("/\\");
    if (found == std::string::npos)
    {
        return {};
    }

    if (found == full_path.length() - 1)
    {
        const size_t prev_found = full_path.find_last_of("/\\", found - 1);
        if (prev_found == std::string::npos)
        {
            return {};
        }
        return full_path.substr(0, prev_found + 1);
    }

    return full_path.substr(0, found + 1);
}

} // namespace dncdbg
