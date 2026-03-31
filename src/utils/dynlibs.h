// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_DYNLIBS_H
#define UTILS_DYNLIBS_H

namespace dncdbg
{
struct DLHandleRef;

// Opaque type representing loaded dynamic library handle.
using DLHandle = DLHandleRef *;

// This function loads the specified library and returns a handle (which then
// can be passed to DLSym and DLClose functions).
// In case of error function returns nullptr.
DLHandle DLOpen(const char *path);

// This function resolves the symbol address within the library specified by handle,
// and returns its address; in case of error, the function returns nullptr.
void *DLSym(DLHandle handle, const char *symbol);

// This function unloads the previously loaded library specified by handle.
// In case of error, this function returns `false`.
bool DLClose(DLHandle handle);

} // namespace dncdbg

#endif // UTILS_DYNLIBS_H
