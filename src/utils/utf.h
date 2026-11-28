// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_UTF_H
#define UTILS_UTF_H

#include <string>
#include <string_view>

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#define W(s) u##s
#else
#include <wtypes.h>
#define W(s) L##s
#endif

namespace dncdbg
{

#ifdef FEATURE_PAL
using WSTRING = std::u16string;
#else
using WSTRING = std::wstring;
#endif

// Convert a null-terminated UTF-16 string to UTF-8 (WCHAR is char16_t on Unix).
// A nullptr input is treated as an empty string.
// Returns an empty string if the input is empty or cannot be converted.
[[nodiscard]] std::string to_utf8(const WCHAR *wstr);

// Convert a UTF-8 string to UTF-16 (WSTRING is std::u16string on Unix).
// Returns an empty string if the input is empty or cannot be converted.
[[nodiscard]] WSTRING to_utf16(std::string_view utf8);

} // namespace dncdbg

#endif // UTILS_UTF_H
