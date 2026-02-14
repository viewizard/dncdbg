// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#include "utils/filesystem.h"
#include "utils/limits.h"
#include <windows.h>
#include <array>

namespace dncdbg
{

// Function returns absolute path to currently running executable.
std::string GetExeAbsPath()
{
    constexpr size_t MAX_LONGPATH = 1024;
    std::array<char, MAX_LONGPATH + 1> hostPath{};
    static const std::string result(hostPath.data(), ::GetModuleFileNameA(nullptr, hostPath.data(), MAX_LONGPATH));
    return result;
}

// Function returns path to directory, which should be used for creation of
// temporary files. Typically this is `/tmp` on Unix and something like
// `C:\Users\localuser\Appdata\Local\Temp` on Windows.
std::string_view GetTempDir()
{
    std::array<char, MAX_PATH + 1> path{};
    static const std::string result(path.data(), GetTempPathA(MAX_PATH, path.data()));
    return result;
}

// Function changes current working directory. Return value is `false` in case of error.
bool SetWorkDir(const std::string &path)
{
    // In the ANSI version of this function, the name is limited to MAX_PATH characters.
    // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcurrentdirectory
    if (path.size() >= MAX_PATH)
    {
        return false;
    }

    return SetCurrentDirectoryA(path.c_str());
}

} // namespace dncdbg
#endif // _WIN32
