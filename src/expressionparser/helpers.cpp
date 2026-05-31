// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "expressionparser/helpers.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include <clocale>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace dncdbg::Parser
{

namespace
{

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

// Multiplies a 96-bit integer (lo, mid, hi) by a short uint32_t multiplier (base 10)
bool multiply96(uint32_t &lo, uint32_t &mid, uint32_t &hi, uint32_t multiplier)
{
    uint64_t carry = 0;

    carry = static_cast<uint64_t>(lo) * multiplier;
    lo = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += static_cast<uint64_t>(mid) * multiplier;
    mid = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += static_cast<uint64_t>(hi) * multiplier;
    hi = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    return carry == 0; // Returns false if overflow occurred (> 96 bits)
}

// Adds a uint32_t value to a 96-bit integer
void add96(uint32_t& lo, uint32_t& mid, uint32_t& hi, uint32_t value)
{
    uint64_t carry = value;

    carry += lo;
    lo = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += mid;
    mid = static_cast<uint32_t>(carry);
    carry >>= bitShift32;

    carry += hi;
    hi = static_cast<uint32_t>(carry);
}

// Helper to extract sign
constexpr std::tuple<bool, std::string_view> parseSign(std::string_view upperText)
{
    if (upperText.empty())
    {
        return { false, upperText };
    }
    if (upperText.front() == '-')
    {
        return { true, upperText.substr(1) };
    }
    if (upperText.front() == '+')
    {
        return { false, upperText.substr(1) };
    }
    return { false, upperText };
}

HRESULT parseDecimal(const std::string &upperText, std::vector<uint8_t> &data, std::string &output)
{
    std::string_view strView = upperText;
    if (!strView.empty() && (strView.back() == 'M'))
    {
        strView.remove_suffix(1);
    }

    if (strView.empty())
    {
        output = "Conversion error: string contains only literal suffix or is empty.";
        return E_INVALIDARG;
    }

    auto [isNegative, numericPart] = parseSign(strView);

    uint32_t hi = 0;
    uint32_t mid = 0;
    uint32_t lo = 0;
    int scale = 0;
    std::optional<size_t> dotIndex = std::nullopt;
    bool hasDigits = false;

    for (size_t i = 0; i < numericPart.length(); ++i)
    {
        const char ch = numericPart.at(i);

        if (ch == '.' || ch == ',')
        {
            if (dotIndex.has_value())
            {
                output = "Conversion error: multiple decimal points.";
                return E_INVALIDARG;
            }
            dotIndex = i;
            continue;
        }

        if (ch >= '0' && ch <= '9')
        {
            hasDigits = true;
            const auto digit = static_cast<uint32_t>(ch - '0');

            if (!multiply96(lo, mid, hi, decimalBase))
            {
                output = "Conversion error: value is too large for .NET Decimal (96-bit mantissa overflow).";
                return E_INVALIDARG;
            }
            add96(lo, mid, hi, digit);

            if (dotIndex.has_value())
            {
                scale++;
            }
        }
        else
        {
            output = "Conversion error: invalid character in numeric string.";
            return E_INVALIDARG;
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

    // Construct the 32-bit flags field
    uint32_t flags = 0;
    flags |= (static_cast<uint32_t>(scale) << scaleShiftBits);
    if (isNegative)
    {
        flags |= (1U << signShiftBits);
    }

    DotNetDecimal dec{ flags, hi, lo, mid };
    data.resize(sizeof(DotNetDecimal), 0);
    std::memcpy(data.data(), &dec, sizeof(DotNetDecimal));
    return S_OK;
}

HRESULT DetermineRealLiteralTypeAndData(const std::string &upperText, CorElementType &type,
                                        std::vector<uint8_t> &data, std::string &output)
{
    if (std::setlocale(LC_NUMERIC, "C") == nullptr)
    {
        LOGW(log << "Failed to set numeric locale to C.");
    }

    if (upperText.back() == 'F') // float
    {
        type = ELEMENT_TYPE_R4;
        data.resize(sizeof(float), 0);

        try
        {
            float value = std::stof(upperText);
            std::memcpy(data.data(), &value, sizeof(float));
        } 
        catch (const std::invalid_argument& e)
        {
            output = "Conversion error: Invalid argument (not a number).";
            return E_INVALIDARG;
        }
        catch (const std::out_of_range& e)
        {
            output = "Conversion error: Value is out of range for a float.";
            return E_INVALIDARG;
        }
    }
    else if (upperText.back() == 'M') // decimal
    {
        type = ELEMENT_TYPE_VALUETYPE;
        return parseDecimal(upperText, data, output);
    }
    else // double (default for real literals)
    {
        type = ELEMENT_TYPE_R8;
        data.resize(sizeof(double), 0);

        try
        {
            double value = std::stod(upperText);
            std::memcpy(data.data(), &value, sizeof(double));
        } 
        catch (const std::invalid_argument& e)
        {
            output = "Conversion error: Invalid argument (not a number).";
            return E_INVALIDARG;
        }
        catch (const std::out_of_range& e)
        {
            output = "Conversion error: Value is out of range for a double.";
            return E_INVALIDARG;
        }
    }

    return S_OK;
}

HRESULT ParseULL(const std::string &upperText, unsigned long long &parsedValue, std::string &output)
{
    try
    {
        parsedValue = std::stoull(upperText);
    } 
    catch (const std::invalid_argument& e)
    {
        output = "Conversion error: does not start with a valid number.";
        return E_INVALIDARG;
    } 
    catch (const std::out_of_range& e)
    {
        output = "Conversion error: Value is too large.";
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT ParseLL(const std::string &upperText, long long &parsedValue, std::string &output)
{
    try
    {
        parsedValue = std::stoll(upperText);
    } 
    catch (const std::invalid_argument& e)
    {
        output = "Conversion error: does not start with a valid number.";
        return E_INVALIDARG;
    } 
    catch (const std::out_of_range& e)
    {
        output = "Conversion error: Value is too large.";
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT DetermineIntegerLiteralTypeAndData(const std::string &upperText, CorElementType &type,
                                           std::vector<uint8_t> &data, std::string &output)
{
    HRESULT Status = S_OK;

    if (upperText.size() >= 2)
    {
        const std::string_view last2 = std::string_view(upperText).substr(upperText.size() - 2);
        if (last2 == "UL" || last2 == "LU")
        {
            type = ELEMENT_TYPE_U8;
            data.resize(sizeof(uint64_t), 0);

            unsigned long long parsedValue = 0;
            IfFailRet(ParseULL(upperText, parsedValue, output));

            if (parsedValue > std::numeric_limits<uint64_t>::max())
            {
                output = "Conversion error: Value is out of range for uint64_t.";
                return E_INVALIDARG;
            }

            auto value = static_cast<uint64_t>(parsedValue);
            std::memcpy(data.data(), &value, sizeof(uint64_t));
            return S_OK;
        }
    }

    const char lastChar = upperText.back();
    if (lastChar == 'U') 
    {
        type = ELEMENT_TYPE_U4;
        data.resize(sizeof(uint32_t), 0);

        unsigned long long parsedValue = 0;
        IfFailRet(ParseULL(upperText, parsedValue, output));

        if (parsedValue > std::numeric_limits<uint32_t>::max())
        {
            output = "Conversion error: Value is out of range for uint32_t.";
            return E_INVALIDARG;
        }

        auto value = static_cast<uint32_t>(parsedValue);
        std::memcpy(data.data(), &value, sizeof(uint32_t));
        return S_OK;
    }

    if (lastChar == 'L')
    {
        type = ELEMENT_TYPE_I8;
        data.resize(sizeof(int64_t), 0);

        long long parsedValue = 0;
        IfFailRet(ParseLL(upperText, parsedValue, output));

        if (parsedValue > std::numeric_limits<int64_t>::max() ||
            parsedValue < std::numeric_limits<int64_t>::min())
        {
            output = "Conversion error: Value is out of range for int64_t.";
            return E_INVALIDARG;
        }

        auto value = static_cast<int64_t>(parsedValue);
        std::memcpy(data.data(), &value, sizeof(int64_t));
        return S_OK;
    }
/*
    // Fallback when there is no suffix (C# chooses the narrowest type that fits)
    try
    {
        unsigned long long val = 0;

        if (upper_text.rfind("0X", 0) == 0) 
        {
            // Handle Hexadecimal (e.g., 0xFA)
            static constexpr int baseHex = 16;
            val = std::stoull(upper_text, nullptr, baseHex);
            
            if (val > UINT32_MAX)
            {
                return ELEMENT_TYPE_I8; // Fits in long / ulong
            }
            if (val > INT32_MAX)
            {
                return ELEMENT_TYPE_U4; // Fits in uint
            }
            return ELEMENT_TYPE_I4; // Fits in standard int
        } 
        else if(upper_text.rfind("0B", 0) == 0)
        {
            // Strip the "0B" prefix so std::stoull can parse it as base 2
            const std::string binary_digits = upper_text.substr(2);
            val = std::stoull(binary_digits, nullptr, 2);
            
            if (val > UINT32_MAX)
            {
                return ELEMENT_TYPE_I8; // Int64
            }
            if (val > INT32_MAX)
            {
                return ELEMENT_TYPE_U4; // UInt32
            }
            return ELEMENT_TYPE_I4; // Int32
        } 
        else
        {
            // Standard decimal number
            const long long dec_val = std::stoll(upper_text);
            if (dec_val < INT32_MIN || dec_val > INT32_MAX)
            {
                return ELEMENT_TYPE_I8; // Int64
            }
            return ELEMENT_TYPE_I4; // Int32
        }
    }
    catch (...)
    {
        return ELEMENT_TYPE_I8; // Int64 - Safe fallback for massive integers
    }
*/
    // Default C# integer type
    type = ELEMENT_TYPE_I4;
    data.resize(sizeof(int32_t), 0);

    long long parsedValue = 0;
    IfFailRet(ParseLL(upperText, parsedValue, output));

    if (parsedValue > std::numeric_limits<int32_t>::max() ||
        parsedValue < std::numeric_limits<int32_t>::min())
    {
        output = "Conversion error: Value is out of range for int32_t.";
        return E_INVALIDARG;
    }

    auto value = static_cast<int32_t>(parsedValue);
    std::memcpy(data.data(), &value, sizeof(int32_t));
    return S_OK;
}

} // unnamed namespace

HRESULT DetermineNumericTypeAndData(const std::string &text, bool realLiteral, CorElementType &type,
                                    std::vector<uint8_t> &data, std::string &output)
{
    if (text.empty())
    {
        return E_INVALIDARG;
    }

    // Convert to uppercase for uniform suffix checking
    std::string upperText;
    upperText.reserve(text.size());
    for (const char c : text)
    {
        upperText.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    // 1. Handle Real Literals (Floating point)
    if (realLiteral)
    {
        return DetermineRealLiteralTypeAndData(upperText, type, data, output);
    }
    // 2. Handle Integer Literals (Check suffixes from longest to shortest)
    return DetermineIntegerLiteralTypeAndData(upperText, type, data, output);
}

HRESULT ParsePredefinedType(const std::string &typeName, CorElementType &type, std::string &output)
{
    static const std::unordered_map<std::string, CorElementType> predefinedTypeMap{
        {"bool", ELEMENT_TYPE_BOOLEAN},
        {"byte", ELEMENT_TYPE_U1},
        {"char", ELEMENT_TYPE_CHAR},
        {"decimal", ELEMENT_TYPE_VALUETYPE},
        {"double", ELEMENT_TYPE_R8},
        {"float", ELEMENT_TYPE_R4},
        {"int", ELEMENT_TYPE_I4},
        {"long", ELEMENT_TYPE_I8},
        {"object", ELEMENT_TYPE_MAX},
        {"sbyte", ELEMENT_TYPE_I1},
        {"short", ELEMENT_TYPE_I2},
        {"string", ELEMENT_TYPE_STRING},
        {"ushort", ELEMENT_TYPE_U2},
        {"uint", ELEMENT_TYPE_U4},
        {"ulong", ELEMENT_TYPE_U8}
    };

    auto find = predefinedTypeMap.find(typeName);
    if (find == predefinedTypeMap.end())
    {
        output = "Unknown predefined type: " + typeName;
        return E_INVALIDARG;
    }

    type = find->second;

    return S_OK;
}

} // namespace dncdbg::Parser
