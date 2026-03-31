// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#include "utils/dynlibs.h"
#include "utils/limits.h"
#include <windows.h>

namespace dncdbg
{

// This function loads the specified library and returns a handle (which then
// can be passed to DLSym and DLClose functions).
// In case of error, the function returns nullptr.
DLHandle DLOpen(const char *path)
{
    return reinterpret_cast<DLHandle>(::LoadLibraryExA(path, nullptr, 0));
}

// This function resolves the symbol address within the library specified by handle,
// and returns its address; in case of error, the function returns nullptr.
void *DLSym(DLHandle handle, const char *symbol)
{
    return ::GetProcAddress((HMODULE)handle, symbol);
}

// This function unloads the previously loaded library specified by handle.
// In case of error, this function returns `false`.
bool DLClose(DLHandle handle)
{
    return ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

} // namespace dncdbg
#endif // _WIN32
