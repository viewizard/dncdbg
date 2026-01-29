// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#pragma once
#include "utils/platform.h"
#include "utils/string_view.h"
#include <cstddef>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include "utils/limits.h"
#include "utils/platform.h"
#include <cstddef>
#elif _WIN32
#include <cstddef>
#include <windows.h>
#endif

namespace dncdbg
{

namespace FileSystem
{
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
const static size_t PathMax = PATH_MAX;
const static size_t NameMax = NAME_MAX;
const static char PathSeparator = '/';
const static char *PathSeparatorSymbols = "/";
#elif _WIN32
const static size_t PathMax = MAX_PATH;
const static size_t NameMax = MAX_PATH - 1; // not include terminal null.
const static char PathSeparator = '\\';
const static char *PathSeparatorSymbols = "/\\";
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
Utility::string_view GetTempDir();

// Function checks, if given path contains directory names (strictly speaking,
// contains path separator) or consists only of a file name. Return value is `true`
// if argument is not the file name, but the path which includes directory names.
bool IsFullPath(const std::string &path);

} // namespace dncdbg
