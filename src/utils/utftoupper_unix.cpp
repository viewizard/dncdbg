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

    static constexpr auto retError = static_cast<std::size_t>(-1);

    // Step 1: Convert UTF-8 to wide string (UTF-32)
    const std::size_t wideSize = std::mbstowcs(nullptr, input.c_str(), 0);
    if (wideSize == retError)
    {
        LOGE(log << "std::mbstowcs (size calculation)");
        return input;
    }

    std::vector<wchar_t> wideBuf(wideSize + 1, L'\0');
    if (retError == std::mbstowcs(wideBuf.data(), input.c_str(), wideBuf.size()))
    {
        LOGE(log << "std::mbstowcs (conversion)");
        return input;
    }

    // Step 2: Convert wide string to uppercase using std::towupper
    for (std::size_t i = 0; i < wideSize; ++i)
    {
        wideBuf.at(i) = static_cast<wchar_t>(std::towupper(wideBuf.at(i)));
    }

    // Step 3: Convert uppercase wide string back to UTF-8
    const std::size_t utf8Size = std::wcstombs(nullptr, wideBuf.data(), 0);
    if (utf8Size == retError)
    {
        LOGE(log << "std::wcstombs (size calculation)");
        return input;
    }

    std::string output(utf8Size, '\0');
    if (retError == std::wcstombs(output.data(), wideBuf.data(), output.size() + 1))
    {
        LOGE(log << "std::wcstombs (conversion)");
        return input;
    }

    return output;
}

} // namespace dncdbg

#endif // defined(FEATURE_PAL) && !defined(__APPLE__)
