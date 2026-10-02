// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_WINEXIT_H
#define UTILS_WINEXIT_H

#ifdef _WIN32

#include <functional>
#include <windows.h>

namespace dncdbg::WinExit
{

void SetupTrackingHook(DWORD pid, std::function<void(int)> exitProcess);
void Cleanup();

} // namespace dncdbg::WinExit

#endif // _WIN32

#endif // UTILS_WINEXIT_H
