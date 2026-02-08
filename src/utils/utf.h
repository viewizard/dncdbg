// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <string>

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <wtypes.h>
#endif

namespace dncdbg
{

#ifdef FEATURE_PAL
using WSTRING = std::u16string;
#else
using WSTRING = std::wstring;
#endif

std::string to_utf8(const WCHAR *wstr);
WSTRING to_utf16(const std::string &utf8);

template <typename CharT, size_t Size> bool starts_with(const CharT *left, const CharT (&right)[Size])
{
    return std::char_traits<CharT>::compare(left, right, Size - 1) == 0;
}

template <typename CharT, size_t Size> bool str_equal(const CharT *left, const CharT (&right)[Size])
{
    return std::char_traits<CharT>::compare(left, right, Size) == 0;
}

} // namespace dncdbg
