// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "expressionparser/helpers.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace dncdbg::Parser
{

namespace
{

constexpr int decimalRadix = 10;
constexpr int hexadecimalRadix = 16;
constexpr int binaryRadix = 2;

struct DotNetDecimal
{
    uint32_t flags = 0;
    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t mid = 0;
};
constexpr uint8_t bitShift32 = 32;
constexpr uint32_t decimalBase = 10;
constexpr int maxDecimalScale = 28;
constexpr uint8_t scaleShiftBits = 16;
constexpr uint8_t signShiftBits = 31;

// Multiplies a 96-bit integer (lo, mid, hi) by a uint32_t multiplier.
// Returns false if the result does not fit into 96 bits.
bool multiply96(uint32_t &lo, uint32_t &mid, uint32_t &hi, uint32_t multiplier)
{
    uint64_t carry = static_cast<uint64_t>(lo) * multiplier;
    lo = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += static_cast<uint64_t>(mid) * multiplier;
    mid = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += static_cast<uint64_t>(hi) * multiplier;
    hi = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    return carry == 0;
}

// Adds a uint32_t value to a 96-bit integer.
// Returns false if the result does not fit into 96 bits.
bool add96(uint32_t &lo, uint32_t &mid, uint32_t &hi, uint32_t value)
{
    uint64_t carry = static_cast<uint64_t>(lo) + value;
    lo = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += mid;
    mid = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += hi;
    hi = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    return carry == 0;
}

// Stores the object representation of value into data.
template <typename T>
void storeValue(std::vector<uint8_t> &data, const T &value)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    data.assign(bytes, bytes + sizeof(T));
}

// Splits an optional leading sign from the text.
std::string_view parseSign(std::string_view text, bool &isNegative)
{
    isNegative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+'))
    {
        isNegative = (text.front() == '-');
        text.remove_prefix(1);
    }
    return text;
}

HRESULT parseDecimal(std::string_view upperText, std::vector<uint8_t> &data, std::string &output)
{
    if (!upperText.empty() && upperText.back() == 'M')
    {
        upperText.remove_suffix(1);
    }

    if (upperText.empty())
    {
        output = "Conversion error: string contains only literal suffix or is empty.";
        return E_INVALIDARG;
    }

    bool isNegative = false;
    const std::string_view numericPart = parseSign(upperText, isNegative);

    uint32_t hi = 0;
    uint32_t mid = 0;
    uint32_t lo = 0;
    int scale = 0;
    bool seenDot = false;
    bool hasDigits = false;

    for (const char ch : numericPart)
    {
        if (ch == '.' || ch == ',')
        {
            if (seenDot)
            {
                output = "Conversion error: multiple decimal points.";
                return E_INVALIDARG;
            }
            seenDot = true;
            continue;
        }

        if (ch < '0' || ch > '9')
        {
            output = "Conversion error: invalid character in numeric string.";
            return E_INVALIDARG;
        }

        hasDigits = true;
        if (!multiply96(lo, mid, hi, decimalBase) ||
            !add96(lo, mid, hi, static_cast<uint32_t>(ch - '0')))
        {
            output = "Conversion error: value is too large for .NET Decimal (96-bit mantissa overflow).";
            return E_INVALIDARG;
        }

        if (seenDot)
        {
            ++scale;
        }
    }

    if (!hasDigits)
    {
        output = "Conversion error: no digits found.";
        return E_INVALIDARG;
    }
    if (scale > maxDecimalScale)
    {
        output = "Conversion error: scale cannot exceed 28 in .NET Decimal.";
        return E_INVALIDARG;
    }

    // Build the 32-bit flags field
    uint32_t flags = static_cast<uint32_t>(scale) << scaleShiftBits;
    if (isNegative)
    {
        flags |= (1U << signShiftBits);
    }

    storeValue(data, DotNetDecimal{flags, hi, lo, mid});
    return S_OK;
}

// Upper-cases the literal and drops '_' digit separators, so that the suffix and prefix checks
// and the conversion functions work on a uniform string.
std::string normalizeLiteral(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const char c : text)
    {
        if (c == '_')
        {
            continue;
        }
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return result;
}

HRESULT DetermineRealLiteralTypeAndData(std::string &text, CorElementType &elemType,
                                        std::vector<uint8_t> &data, std::string &output)
{
    if (text.back() == 'M') // decimal
    {
        elemType = ELEMENT_TYPE_VALUETYPE;
        return parseDecimal(text, data, output);
    }

    // Strip the type suffix. c_str() always returns a NUL-terminated string, so the text can be
    // passed directly to strtod/strtof.
    const bool isFloat = (text.back() == 'F');
    if (isFloat || text.back() == 'D')
    {
        text.pop_back();
    }

    const char *begin = text.c_str();
    char *end = nullptr;
    errno = 0;

    if (isFloat)
    {
        elemType = ELEMENT_TYPE_R4;
        const float value = std::strtof(begin, &end);
        if (end == begin || *end != '\0')
        {
            output = "Conversion error: Invalid argument (not a number).";
            return E_INVALIDARG;
        }
        if (errno == ERANGE)
        {
            output = "Conversion error: Value is out of range for a float.";
            return E_INVALIDARG;
        }
        storeValue(data, value);
        return S_OK;
    }

    // double (default for real literals)
    elemType = ELEMENT_TYPE_R8;
    const double value = std::strtod(begin, &end);
    if (end == begin || *end != '\0')
    {
        output = "Conversion error: Invalid argument (not a number).";
        return E_INVALIDARG;
    }
    if (errno == ERANGE)
    {
        output = "Conversion error: Value is out of range for a double.";
        return E_INVALIDARG;
    }
    storeValue(data, value);
    return S_OK;
}

HRESULT DetermineIntegerLiteralTypeAndData(std::string &text, CorElementType &elemType,
                                           std::vector<uint8_t> &data, std::string &output)
{
    // Strip the type suffix (longest match first). 'U' and 'L' are not hexadecimal digits,
    // so they can never be confused with the digits of a hexadecimal literal.
    bool isUnsigned = false;
    bool isLong = false;
    if (text.size() >= 2 &&
        (text.compare(text.size() - 2, 2, "UL") == 0 || text.compare(text.size() - 2, 2, "LU") == 0))
    {
        isUnsigned = true;
        isLong = true;
        text.resize(text.size() - 2);
    }
    else if (text.back() == 'U')
    {
        isUnsigned = true;
        text.pop_back();
    }
    else if (text.back() == 'L')
    {
        isLong = true;
        text.pop_back();
    }

    // Radix prefix, examined after the suffix has been removed.
    int base = decimalRadix;
    std::string_view digits = text;
    if (text.compare(0, 2, "0X") == 0)
    {
        base = hexadecimalRadix;
        digits.remove_prefix(2);
    }
    else if (text.compare(0, 2, "0B") == 0)
    {
        base = binaryRadix;
        digits.remove_prefix(2);
    }

    uint64_t value = 0;
    const char *first = digits.data();
    const char *last = first + digits.size();
    const auto [ptr, ec] = std::from_chars(first, last, value, base);
    if (ec == std::errc::result_out_of_range)
    {
        output = "Conversion error: Value is too large.";
        return E_INVALIDARG;
    }
    if (ec != std::errc{} || ptr != last)
    {
        output = "Conversion error: does not start with a valid number.";
        return E_INVALIDARG;
    }

    if (isUnsigned && isLong)
    {
        elemType = ELEMENT_TYPE_U8;
        storeValue(data, static_cast<uint64_t>(value));
        return S_OK;
    }

    if (isUnsigned)
    {
        if (value > std::numeric_limits<uint32_t>::max())
        {
            output = "Conversion error: Value is out of range for UInt32.";
            return E_INVALIDARG;
        }
        elemType = ELEMENT_TYPE_U4;
        storeValue(data, static_cast<uint32_t>(value));
        return S_OK;
    }

    if (isLong)
    {
        if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        {
            output = "Conversion error: Value is out of range for Int64.";
            return E_INVALIDARG;
        }
        elemType = ELEMENT_TYPE_I8;
        storeValue(data, static_cast<int64_t>(value));
        return S_OK;
    }

    if (base == decimalRadix)
    {
        if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        {
            output = "Conversion error: Value is out of range for Int32.";
            return E_INVALIDARG;
        }
        elemType = ELEMENT_TYPE_I4;
        storeValue(data, static_cast<int32_t>(value));
        return S_OK;
    }

    // Hexadecimal and binary literals without a suffix take the first type that can hold the value.
    if (value <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
    {
        elemType = ELEMENT_TYPE_I4;
        storeValue(data, static_cast<int32_t>(value));
    }
    else if (value <= std::numeric_limits<uint32_t>::max())
    {
        elemType = ELEMENT_TYPE_U4;
        storeValue(data, static_cast<uint32_t>(value));
    }
    else if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        elemType = ELEMENT_TYPE_I8;
        storeValue(data, static_cast<int64_t>(value));
    }
    else
    {
        elemType = ELEMENT_TYPE_U8;
        storeValue(data, static_cast<uint64_t>(value));
    }
    return S_OK;
}

struct PredefinedTypeEntry
{
    std::string_view name;
    CorElementType elemType;
};

constexpr size_t predefinedTypeCount = 17;
constexpr std::array<PredefinedTypeEntry, predefinedTypeCount> predefinedTypes{{
    {"bool",    ELEMENT_TYPE_BOOLEAN},
    {"byte",    ELEMENT_TYPE_U1},
    {"char",    ELEMENT_TYPE_CHAR},
    {"decimal", ELEMENT_TYPE_VALUETYPE},
    {"double",  ELEMENT_TYPE_R8},
    {"float",   ELEMENT_TYPE_R4},
    {"int",     ELEMENT_TYPE_I4},
    {"long",    ELEMENT_TYPE_I8},
    {"object",  ELEMENT_TYPE_OBJECT},
    {"sbyte",   ELEMENT_TYPE_I1},
    {"short",   ELEMENT_TYPE_I2},
    {"string",  ELEMENT_TYPE_STRING},
    {"ushort",  ELEMENT_TYPE_U2},
    {"uint",    ELEMENT_TYPE_U4},
    {"ulong",   ELEMENT_TYPE_U8},
    {"nint",    ELEMENT_TYPE_I},
    {"nuint",   ELEMENT_TYPE_U}
}};

} // unnamed namespace

HRESULT DetermineNumericTypeAndData(std::string_view text, bool realLiteral, CorElementType &elemType,
                                    std::vector<uint8_t> &data, std::string &output)
{
    std::string normalized = normalizeLiteral(text);
    if (normalized.empty())
    {
        output = "Conversion error: empty numeric literal.";
        return E_INVALIDARG;
    }

    if (realLiteral)
    {
        return DetermineRealLiteralTypeAndData(normalized, elemType, data, output);
    }
    return DetermineIntegerLiteralTypeAndData(normalized, elemType, data, output);
}

HRESULT ParsePredefinedType(std::string_view typeName, CorElementType &elemType, std::string &output)
{
    const auto findType = std::find_if(std::begin(predefinedTypes), std::end(predefinedTypes),
                                       [typeName](const PredefinedTypeEntry &entry) { return entry.name == typeName; });
    if (findType == std::end(predefinedTypes))
    {
        output = "Unknown predefined type: " + std::string(typeName);
        return E_INVALIDARG;
    }

    elemType = findType->elemType;
    return S_OK;
}

} // namespace dncdbg::Parser
