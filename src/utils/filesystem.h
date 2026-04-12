// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_FILESYSTEM_H
#define UTILS_FILESYSTEM_H

#include <string>
#include <string_view>

namespace dncdbg
{

namespace FileSystem
{
#ifdef FEATURE_PAL
constexpr char PathSeparator = '/';
constexpr std::string_view PathSeparatorSymbols("/");
#else
constexpr char PathSeparator = '\\';
constexpr std::string_view PathSeparatorSymbols("/\\");
#endif
} // namespace FileSystem

// Function returns absolute path to currently running executable.
std::string GetExeAbsPath();

// Function returns only file name part of the full path.
std::string GetBasename(const std::string &path);

// Function changes current working directory. Return value is `false` in case of error.
bool SetWorkDir(const std::string &path);

// Function returns path to directory, which should be used for creation of
// temporary files. Typically this is `/tmp` on Unix and something like
// `C:\Users\localuser\Appdata\Local\Temp` on Windows.
std::string_view GetTempDir();

// Function checks, if given path contains directory names (strictly speaking,
// contains path separator) or consists only of a file name. Return value is `true`
// if argument is not the file name, but the path which includes directory names.
bool IsFullPath(const std::string &path);

std::string GetFileName(const std::string &path);

} // namespace dncdbg

#endif // UTILS_FILESYSTEM_H
