// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/print.h"
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace dncdbg
{

namespace
{

// Print a floating-point value using the shortest representation that round-trips
// to the same value, formatted the way the default C# floating-point conversion does:
// fixed-point notation for moderate magnitudes and scientific notation (e.g. "1E+09")
// for very small and very large ones.
template<typename T>
std::string PrintFloatingPointValue(const T value)
{
    if (std::isnan(value))
    {
        return "NaN";
    }
    if (std::isinf(value))
    {
        return ((value > 0) ? "Infinity" : "-Infinity");
    }

    static constexpr size_t bufferSize = 64;
    std::array<char, bufferSize> buffer{};

    // The std::to_chars overload without a format parameter produces the shortest
    // string that round-trips to the same value, in fixed-point or scientific notation.
    auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + bufferSize, value);
    if (ec != std::errc{})
    {
        return "<error>";
    }

    const std::string_view str(buffer.data(), static_cast<size_t>(ptr - buffer.data()));

    // Parse the to_chars output: [-]digits[.digits][e[-]digits].
    const bool isNegative = (str.front() == '-');
    const size_t mantissaBegin = isNegative ? 1U : 0U;
    const size_t expPos = str.find_first_of("eE");
    const std::string_view mantissa = str.substr(mantissaBegin,
        (expPos == std::string_view::npos ? str.size() : expPos) - mantissaBegin);

    // Mantissa digits without the decimal separator, e.g. "9.9" -> "99".
    std::string digits;
    digits.reserve(mantissa.size());
    size_t integerDigits = 0;
    bool dotFound = false;
    for (const char c : mantissa)
    {
        if (c == '.')
        {
            dotFound = true;
            continue;
        }
        if (!dotFound)
        {
            ++integerDigits;
        }
        digits.push_back(c);
    }

    // Exponent part, e.g. "+14" in "1e+14".
    static constexpr int decimalBase = 10;
    int explicitExponent = 0;
    if (expPos != std::string_view::npos)
    {
        size_t i = expPos + 1;
        bool expNegative = false;
        if (str.at(i) == '-' || str.at(i) == '+')
        {
            expNegative = (str.at(i) == '-');
            ++i;
        }
        for (; i < str.size(); ++i)
        {
            explicitExponent = (explicitExponent * decimalBase) + (str.at(i) - '0');
        }
        if (expNegative)
        {
            explicitExponent = -explicitExponent;
        }
    }

    // The value is 0.<digits> * 10^scale.
    int scale = explicitExponent + static_cast<int>(integerDigits);

    // Strip the leading zeros (e.g. "0.0001" yields digits "00001"), adjusting the scale.
    const size_t firstSignificant = digits.find_first_not_of('0');
    if (firstSignificant == std::string::npos)
    {
        // Zero (possibly negative, e.g. "-0").
        return (isNegative ? "-0" : "0");
    }
    digits.erase(0, firstSignificant);
    scale -= static_cast<int>(firstSignificant);

    // The decimal exponent of the first significant digit: value = d.ddd * 10^exponent.
    const int exponent = scale - 1;

    // Uses the same magnitude thresholds as the default C# floating-point formatting:
    // fixed-point notation for decimal exponents within [-4, max_digits10 - 1]
    // (e.g. "100000000" for float, "10000000000000000" for double), scientific
    // notation otherwise (e.g. "1E+09" for float, "1E+17" for double, "9E-05").
    static constexpr int minFixedExponent = -4;
    const int maxFixedExponent = std::numeric_limits<T>::max_digits10 - 1;

    std::string result;
    if (isNegative)
    {
        result.push_back('-');
    }

    if (exponent < minFixedExponent || exponent > maxFixedExponent)
    {
        // Scientific notation, e.g. "1.7976931348623157E+308".
        result.push_back(digits.front());
        if (digits.size() > 1)
        {
            result.push_back('.');
            result.append(digits, 1, digits.size() - 1);
        }
        result.push_back('E');
        result.push_back((exponent < 0) ? '-' : '+');

        std::string exponentText = std::to_string((exponent < 0) ? -exponent : exponent);
        static constexpr size_t minExponentDigits = 2;
        if (exponentText.size() < minExponentDigits)
        {
            exponentText.insert(0, minExponentDigits - exponentText.size(), '0');
        }
        result.append(exponentText);
    }
    else if (scale <= 0)
    {
        // Fractional value without an integer part, e.g. "0.0001".
        result.append("0.");
        result.append(static_cast<size_t>(-scale), '0');
        result.append(digits);
    }
    else if (static_cast<size_t>(scale) >= digits.size())
    {
        // Integer value, e.g. "100000000".
        result.append(digits);
        result.append(static_cast<size_t>(scale) - digits.size(), '0');
    }
    else
    {
        // Value with both integer and fractional parts, e.g. "9.9".
        result.append(digits, 0, static_cast<size_t>(scale));
        result.push_back('.');
        result.append(digits, static_cast<size_t>(scale), digits.size() - static_cast<size_t>(scale));
    }

    return result;
}

} // unnamed namespace

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

std::string PrintFloat(float value)
{
    return PrintFloatingPointValue(value);
}

std::string PrintDouble(double value)
{
    return PrintFloatingPointValue(value);
}

} // namespace dncdbg
