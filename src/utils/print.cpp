// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/print.h"
#include <iomanip>
#include <sstream>

namespace dncdbg
{

std::string PrintGUID(const GUID &guid)
{
    std::ostringstream ss;
    static constexpr uint32_t widthGuid8 = 8;
    static constexpr uint32_t widthGuid4 = 4;
    static constexpr uint32_t widthGuid2 = 2;
    ss << std::hex << std::setfill('0')
        << std::setw(widthGuid8) << guid.Data1 << '-'
        << std::setw(widthGuid4) << guid.Data2 << '-'
        << std::setw(widthGuid4) << guid.Data3 << '-'
        << std::setw(widthGuid2) << static_cast<int>(guid.Data4[0])
        << std::setw(widthGuid2) << static_cast<int>(guid.Data4[1]) << '-';
    static constexpr size_t startIndex = 2;
    static constexpr size_t endIndex = 8;
    for (size_t i = startIndex; i < endIndex; ++i)
    {
        ss << std::setw(2) << static_cast<int>(guid.Data4[i]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }

    return ss.str();
}

} // namespace dncdbg
