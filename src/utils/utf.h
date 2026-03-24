// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_UTF_H
#define UTILS_UTF_H

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

} // namespace dncdbg

#endif // UTILS_UTF_H
