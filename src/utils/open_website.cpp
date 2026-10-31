// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/open_website.h"

#include "utils/logger.h"
#include <string>

#ifdef _WIN32

#include "utils/utf.h"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#elif defined(FEATURE_PAL)

#include "utils/platform.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

namespace dncdbg
{

#ifdef _WIN32

// Opens `urlUtf8Str` in the default web browser.
bool OpenWebsiteURL(const std::string &urlUtf8Str)
{
    if (urlUtf8Str.empty())
    {
        LOGE(log << "URL is empty");
        return false;
    }

    const WSTRING urlWStr = to_utf16(urlUtf8Str);
    if (urlWStr.empty())
    {
        LOGE(log << "URL is not valid UTF-8: " << urlUtf8Str);
        return false;
    }

    // ShellExecuteEx can delegate execution to Shell extensions that are
    // activated via COM, so COM must be initialized on the calling thread
    // (see the Microsoft documentation). RPC_E_CHANGED_MODE means the thread
    // is already initialized with a different apartment model, and the launch
    // is still attempted.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
    {
        LOGE(log << "CoInitializeEx failed: 0x" << std::hex << comResult);
        return false;
    }
    const bool comInitialized = SUCCEEDED(comResult);

    // SEE_MASK_NOASYNC makes the call synchronous. This is required because
    // `urlWStr` is only valid until this function returns. SEE_MASK_FLAG_NO_UI
    // suppresses error message boxes, because the debugger is a console
    // application.
    SHELLEXECUTEINFOW shellExecuteInfo{};
    shellExecuteInfo.cbSize = sizeof(shellExecuteInfo);
    shellExecuteInfo.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    shellExecuteInfo.lpVerb = L"open";
    shellExecuteInfo.lpFile = urlWStr.c_str();
    shellExecuteInfo.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&shellExecuteInfo) == FALSE)
    {
        const DWORD error = GetLastError();
        if (comInitialized)
        {
            CoUninitialize();
        }
        LOGE(log << "ShellExecuteEx failed for URL \"" << urlUtf8Str << "\", error=" << error);
        return false;
    }

    if (comInitialized)
    {
        CoUninitialize();
    }

    return true;
}

#elif defined(FEATURE_PAL)

// Opens `urlUtf8Str` in the default web browser.
bool OpenWebsiteURL(const std::string &urlUtf8Str)
{
    if (urlUtf8Str.empty())
    {
        LOGE(log << "URL is empty");
        return false;
    }

    // `open` (macOS) asks LaunchServices to open the URL in the default
    // browser, while `xdg-open` (other UNIX systems) delegates URL handling to
    // the desktop environment. Both are started directly, without a shell, so
    // the URL is never parsed by a shell. The child's stdout is redirected to
    // /dev/null because the DAP protocol stream uses the debugger's stdout.
    // Note: posix_spawnp() takes a non-const `argv`, but it does not modify the
    // strings, so the const_casts are safe here.
#if (defined(__APPLE__) && defined(__MACH__))
    const char *const browserProgram = "open";
#else
    const char *const browserProgram = "xdg-open";
#endif

    const std::array<char *, 3> argv = {
        const_cast<char *>(browserProgram), // NOLINT(cppcoreguidelines-pro-type-const-cast)
        const_cast<char *>(urlUtf8Str.c_str()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
        nullptr
    };

    posix_spawn_file_actions_t fileActions{};
    if (posix_spawn_file_actions_init(&fileActions) != 0)
    {
        LOGE(log << "posix_spawn_file_actions_init failed: " << strerror(errno));
        return false;
    }

    if (posix_spawn_file_actions_addopen(&fileActions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0)
    {
        LOGE(log << "posix_spawn_file_actions_addopen failed: " << strerror(errno));
        posix_spawn_file_actions_destroy(&fileActions);
        return false;
    }

    pid_t pid = -1;
    const int spawnResult = posix_spawnp(&pid, browserProgram, &fileActions, nullptr,
                                         argv.data(), GetSystemEnvironment());
    posix_spawn_file_actions_destroy(&fileActions);
    if (spawnResult != 0)
    {
        LOGE(log << "posix_spawnp failed for \"" << browserProgram << "\": " << strerror(spawnResult));
        return false;
    }

    // `open` normally exits as soon as LaunchServices accepts the request, and
    // `xdg-open` usually exits once it has started the handler. Waiting for the
    // child also reaps it and allows its exit status to be checked.
    int status = 0;
    for (;;)
    {
        if (waitpid(pid, &status, 0) == pid)
        {
            break;
        }
        if (errno != EINTR)
        {
            // The child has been spawned; only its exit status remains unknown.
            LOGW(log << "waitpid failed for \"" << browserProgram << "\": " << strerror(errno));
            return true;
        }
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#endif

} // namespace dncdbg
