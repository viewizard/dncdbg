// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_OPEN_WEBSITE_H
#define UTILS_OPEN_WEBSITE_H

#include <string>

namespace dncdbg
{

// Opens `urlUtf8Str` in the default web browser.
// Returns `false` on failure.
bool OpenWebsiteURL(const std::string &urlUtf8Str);

} // namespace dncdbg

#endif // UTILS_OPEN_WEBSITE_H
