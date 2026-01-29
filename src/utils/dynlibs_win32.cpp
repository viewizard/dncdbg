// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#include "utils/dynlibs.h"
#include "utils/limits.h"
#include <windows.h>

namespace dncdbg
{

// This functon load specified library and returns handle (which then
// can be passed to DLSym and DLCLose functions).
// In case of error function returns NULL.
DLHandle DLOpen(const std::string &path)
{
    return reinterpret_cast<DLHandle>(::LoadLibraryExA(path.c_str(), NULL, 0));
}

// This function resolves symbol address within library specified by handle,
// and returns it's address, in case of error function returns NULL.
void *DLSym(DLHandle handle, Utility::string_view name)
{
    char str[LINE_MAX];
    if (name.size() >= sizeof(str))
        return {};

    name.copy(str, name.size());
    str[name.size()] = 0;
    return ::GetProcAddress((HMODULE)handle, str);
}

// This function unloads previously loadded library, specified by handle.
// In case of error this function returns `false'.
bool DLClose(DLHandle handle)
{
    return ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

} // namespace dncdbg
#endif // _WIN32
