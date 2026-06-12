// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if defined(FEATURE_PAL) && !defined(__APPLE__)

#include "utils/utftoupper.h"
#include "utils/logger.h"
#include <unicode/ustring.h>
#include <vector>

namespace dncdbg
{

std::string to_uppercase(const std::string &input)
{
    if (input.empty())
    {
        return {};
    }

    // Step 1: Convert UTF-8 to wide string (UTF-16)
    UErrorCode status = U_ZERO_ERROR;
    int32_t u16_len = 0;
    u_strFromUTF8(nullptr, 0, &u16_len, input.data(), static_cast<int32_t>(input.size()), &status);

    if (status != U_BUFFER_OVERFLOW_ERROR && status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strFromUTF8 (size calculation)");
        return input;
    }

    std::vector<UChar> u16_buf(u16_len);
    status = U_ZERO_ERROR;
    u_strFromUTF8(u16_buf.data(), u16_len, nullptr, input.data(), static_cast<int32_t>(input.size()), &status);

    if (status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strFromUTF8 (conversion)");
        return input;
    }

    // Step 2: Convert UTF-16 to uppercase
    status = U_ZERO_ERROR;
    const int32_t dest_u16_len = u_strToUpper(nullptr, 0, u16_buf.data(), u16_len, "", &status);

    if (status != U_BUFFER_OVERFLOW_ERROR && status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strToUpper (size calculation)");
        return input;
    }

    std::vector<UChar> dest_u16_buf(dest_u16_len);
    status = U_ZERO_ERROR;
    u_strToUpper(dest_u16_buf.data(), dest_u16_len, u16_buf.data(), u16_len, "", &status);

    if (status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strToUpper (conversion)");
        return input;
    }

    // Step 3: Convert back to UTF-8
    status = U_ZERO_ERROR;
    int32_t utf8_len = 0;
    u_strToUTF8(nullptr, 0, &utf8_len, dest_u16_buf.data(), dest_u16_len, &status);

    if (status != U_BUFFER_OVERFLOW_ERROR && status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strToUTF8 (size calculation)");
        return input;
    }

    std::string result(utf8_len, 0);
    status = U_ZERO_ERROR;
    u_strToUTF8(result.data(), utf8_len, nullptr, dest_u16_buf.data(), dest_u16_len, &status);

    if (status > U_ZERO_ERROR)
    {
        LOGE(log << "u_strToUTF8 (conversion)");
        return input;
    }

    return result;
}

} // namespace dncdbg

#endif // defined(FEATURE_PAL) && !defined(__APPLE__)
