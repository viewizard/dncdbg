// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#pragma once

#include <string>
#include <string_view>

namespace dncdbg
{
struct DLHandleRef;

// Opaque type representing loaded dynamic library handle.
typedef DLHandleRef *DLHandle;

// This functon load specified library and returns handle (which then
// can be passed to DLSym and DLCLose functions).
// In case of error function returns NULL.
DLHandle DLOpen(const std::string &path);

// This function resolves symbol address within library specified by handle,
// and returns it's address, in case of error function returns NULL.
void *DLSym(DLHandle handle, const std::string_view &symbol);

// This function unloads previously loadded library, specified by handle.
// In case of error this function returns `false'.
bool DLClose(DLHandle handle);

} // namespace dncdbg
