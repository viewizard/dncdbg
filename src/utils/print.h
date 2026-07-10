// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_PRINT_H
#define UTILS_PRINT_H

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <wtypes.h>
#endif
#include <string>

namespace dncdbg
{

std::string PrintGUID(const GUID &guid);

} // namespace dncdbg

#endif // UTILS_PRINT_H
