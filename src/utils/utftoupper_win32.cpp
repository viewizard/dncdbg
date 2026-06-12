// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/utftoupper.h"
#include "utils/logger.h"
#include <windows.h>

namespace dncdbg
{

std::string to_uppercase(const std::string &input)
{
    if (input.empty())
    {
        return {};
    }

    // Step 1: Convert UTF-8 to wide string (UTF-16)
    const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                             static_cast<int>(input.size()), nullptr, 0);

    if (wideSize <= 0)
    {
        LOGE(log << "MultiByteToWideChar (size calculation)");
        return input;
    }

    std::wstring wideStr(wideSize, L'\0');
    const int wideResult = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                               static_cast<int>(input.size()), wideStr.data(), wideSize);

    if (wideResult <= 0)
    {
        LOGE(log << "MultiByteToWideChar (conversion)");
        return input;
    }

    // Step 2: Convert wide string to uppercase using LCMapStringEx
    const int upperWideSize = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, wideStr.data(),
                                            wideResult, nullptr, 0, nullptr, nullptr, 0);

    if (upperWideSize <= 0)
    {
        LOGE(log << "LCMapStringEx (size calculation)");
        return input;
    }

    std::wstring upperWideStr(upperWideSize, L'\0');
    const int upperResult = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, wideStr.data(), wideResult,
                                          upperWideStr.data(), upperWideSize, nullptr, nullptr, 0);

    if (upperResult <= 0)
    {
        LOGE(log << "LCMapStringEx (conversion)");
        return input;
    }

    // Step 3: Convert uppercase wide string back to UTF-8
    const int resultSize = WideCharToMultiByte(CP_UTF8, 0, upperWideStr.data(), upperResult,
                                               nullptr, 0, nullptr, nullptr);

    if (resultSize <= 0)
    {
        LOGE(log << "WideCharToMultiByte (size calculation)");
        return input;
    }

    std::string result(resultSize, '\0');
    const int finalResult = WideCharToMultiByte(CP_UTF8, 0, upperWideStr.data(), upperResult,
                                                result.data(), resultSize, nullptr, nullptr);

    if (finalResult <= 0)
    {
        LOGE(log << "WideCharToMultiByte (conversion)");
        return input;
    }

    return result;
}

} // namespace dncdbg

#endif // _WIN32
