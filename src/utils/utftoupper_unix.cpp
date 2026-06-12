// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if defined(FEATURE_PAL) && !defined(__APPLE__)

#include "utils/utftoupper.h"
#include "utils/logger.h"
#include <vector>

namespace dncdbg
{

std::string to_uppercase(const std::string &input)
{
    if (input.empty())
    {
        return input;
    }

    // Step 1: Convert UTF-8 to wide string (UTF-32)
    const size_t wide_size = std::mbstowcs(nullptr, input.c_str(), 0);
    if (wide_size == static_cast<size_t>(-1))
    {
        LOGE(log << "std::mbstowcs (size calculation)");
        return input;
    }

    std::vector<wchar_t> wide_buf(wide_size + 1, L'\0');
    if (static_cast<size_t>(-1) == std::mbstowcs(wide_buf.data(), input.c_str(), wide_buf.size()))
    {
        LOGE(log << "std::mbstowcs (conversion)");
        return input;
    }

    // Step 2: Convert wide string to uppercase using std::towupper
    for (size_t i = 0; i < wide_size; ++i)
    {
        wide_buf.at(i) = static_cast<wchar_t>(std::towupper(wide_buf.at(i)));
    }

    // Step 3: Convert uppercase wide string back to UTF-8
    const size_t utf8_size = std::wcstombs(nullptr, wide_buf.data(), 0);
    if (utf8_size == static_cast<size_t>(-1))
    {
        LOGE(log << "std::wcstombs (size calculation)");
        return input;
    }

    std::string output(utf8_size, '\0');
    if (static_cast<size_t>(-1) == std::wcstombs(output.data(), wide_buf.data(), output.size() + 1))
    {
        LOGE(log << "std::wcstombs (conversion)");
        return input;
    }

    return output;
}

} // namespace dncdbg

#endif // defined(FEATURE_PAL) && !defined(__APPLE__)
