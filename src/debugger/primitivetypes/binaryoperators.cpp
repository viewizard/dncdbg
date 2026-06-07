// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/primitivetypes/types.h"
#include <cassert>
#include <cmath>
#include <functional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace dncdbg::PrimitiveTypes
{

namespace
{

constexpr int maxShift4Byte = 32;
constexpr int maxShift8Byte = 64;

void FillBinaryErrorOutput(const std::string_view &opName, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, std::string &output)
{
    std::ostringstream ss;
    std::visit(
        [&](auto &arg1, auto &arg2) -> void
        {
            ss << "error: Operator '" << opName << "' cannot be applied to operands of type '" << TypeMapping<std::decay_t<decltype(arg1)>>::description
               << "' and '" << TypeMapping<std::decay_t<decltype(arg2)>>::description << "'";
        }, leftValue, rightValue);
    output = ss.str();
}

void FillBinaryAmbiguousErrorOutput(const std::string_view &opName, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, std::string &output)
{
    std::ostringstream ss;
    std::visit(
        [&](auto &arg1, auto &arg2) -> void
        {
            ss << "error: Operator '" << opName << "' is ambiguous on operands of type '" << TypeMapping<std::decay_t<decltype(arg1)>>::description
               << "' and '" << TypeMapping<std::decay_t<decltype(arg2)>>::description << "'";
        }, leftValue, rightValue);
    output = ss.str();
}

void FillBinaryConvertErrorOutput(const PrimitiveValue &primValue, std::string &output)
{
    std::ostringstream ss;
    std::visit(
        [&](auto &arg) -> void
        {
            ss << "error: Cannot implicitly convert type '" << TypeMapping<std::decay_t<decltype(arg)>>::description << "' to 'bool'";
        }, primValue);
    output = ss.str();
}

// Helper template to convert PrimitiveValue to target numeric type.
// Handles int8_t specially by casting through uint8_t to preserve bit pattern.
template <typename TargetType>
TargetType ConvertToNumeric(const PrimitiveValue &value)
{
    TargetType result{};
    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "value not properly initialized."); },
        [](const bool &) { assert(false && "value not supported."); },
        [](const std::string &) { assert(false && "value not supported."); },
        [&result](auto &arg) { result = static_cast<TargetType>(arg); } // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
    }, value);
    return result;
}

HRESULT AddExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "+";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto ConvertToString =
        [&](const PrimitiveValue &value) -> std::string
        {
            std::string ret;
            std::visit(overloaded {
                [](const std::monostate &) { assert(false && "value not properly initialized."); },
                [&](const bool &arg) { ret = arg ? "True" : "False"; },
                [&](const WCHAR &arg) { WSTRING tmp(2, '\0'); tmp.at(0) = arg; ret = to_utf8(tmp.c_str()); },
                [&](const std::string &arg) { ret = arg; },
                [&](const double &arg) { std::ostringstream ss; ss << arg; ret = ss.str(); },
                [&](const float &arg) { std::ostringstream ss; ss << arg; ret = ss.str(); },
                [&](auto &arg) { ret = std::to_string(arg); },
            }, value);
            return ret;
        };

    if (std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        const std::string leftStrValue = ConvertToString(leftValue);
        const std::string rightStrValue = ConvertToString(rightValue);
        outputValue.emplace<std::string>(leftStrValue + rightStrValue);
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(ConvertToNumeric<double>(leftValue) + ConvertToNumeric<double>(rightValue));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(ConvertToNumeric<float>(leftValue) + ConvertToNumeric<float>(rightValue));
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) + ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) + ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) + ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) + ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT SubtractExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "-";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(ConvertToNumeric<double>(leftValue) - ConvertToNumeric<double>(rightValue));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(ConvertToNumeric<float>(leftValue) - ConvertToNumeric<float>(rightValue));
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) - ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) - ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) - ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) - ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT MultiplyExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "*";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(ConvertToNumeric<double>(leftValue) * ConvertToNumeric<double>(rightValue));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(ConvertToNumeric<float>(leftValue) * ConvertToNumeric<float>(rightValue));
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) * ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) * ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) * ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) * ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT DivideExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "/";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(ConvertToNumeric<double>(leftValue) / ConvertToNumeric<double>(rightValue));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(ConvertToNumeric<float>(leftValue) / ConvertToNumeric<float>(rightValue));
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) / ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) / ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) / ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) / ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT ModuloExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "%";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(std::fmod(ConvertToNumeric<double>(leftValue), ConvertToNumeric<double>(rightValue)));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(std::fmod(ConvertToNumeric<float>(leftValue), ConvertToNumeric<float>(rightValue)));
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) % ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) % ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) % ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) % ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT LeftShiftExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "<<";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
        std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
                                                          std::holds_alternative<uint32_t>(rightValue) ||
                                                          std::holds_alternative<uint64_t>(rightValue) ||
                                                          std::holds_alternative<int64_t>(rightValue))
    {
        setError();
    }
    else if (std::holds_alternative<int64_t>(leftValue))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) << (ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint64_t>(leftValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) << (ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint32_t>(leftValue))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) << (ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) << (ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }

    return Status;
}

HRESULT RightShiftExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = ">>";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
        std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
        std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
                                                          std::holds_alternative<uint32_t>(rightValue) ||
                                                          std::holds_alternative<uint64_t>(rightValue) ||
                                                          std::holds_alternative<int64_t>(rightValue))
    {
        setError();
    }
    else if (std::holds_alternative<int64_t>(leftValue))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) >> (ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint64_t>(leftValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) >> (ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint32_t>(leftValue))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) >> (ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) >> (ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }

    return Status;
}

HRESULT BitwiseAndExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "&";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) && std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
             std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
             std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
             (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) & ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) & ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) & ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) & ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT BitwiseOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "|";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) || std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
             std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
             std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
             (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) | ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) | ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) | ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) | ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT ExclusiveOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "^";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) ^ std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue) ||
             std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
             std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
             (std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setError();
    }
    else if (std::holds_alternative<int64_t>(leftValue) || std::holds_alternative<int64_t>(rightValue) ||
             (std::holds_alternative<uint32_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue))) ||
             (std::holds_alternative<uint32_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue))))
    {
        outputValue.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue) ^ ConvertToNumeric<int64_t>(rightValue));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue) ^ ConvertToNumeric<uint64_t>(rightValue));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue) ^ ConvertToNumeric<uint32_t>(rightValue));
    }
    else
    {
        outputValue.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue) ^ ConvertToNumeric<int32_t>(rightValue));
    }

    return Status;
}

HRESULT LogicalAndExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "&&";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setConvertError =
        [&](const PrimitiveValue &primValue) -> void
        {
            FillBinaryConvertErrorOutput(primValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {

        outputValue.emplace<bool>(std::get<bool>(leftValue) && std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue))
    {
            setError();
    }
    else
    {
        setConvertError(leftValue);
    }

    return Status;
}

HRESULT LogicalOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "||";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setConvertError =
        [&](const PrimitiveValue &primValue) -> void
        {
            FillBinaryConvertErrorOutput(primValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) || std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue))
    {
        setError();
    }
    else
    {
        setConvertError(leftValue);
    }

    return Status;
}

HRESULT EqualsExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "==";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) == std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<std::string>(leftValue) && std::holds_alternative<std::string>(rightValue))
    {
;
        outputValue.emplace<bool>(std::get<std::string>(leftValue) == std::get<std::string>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 == arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) == ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

HRESULT NotEqualsExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "!=";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(std::get<bool>(leftValue) != std::get<bool>(rightValue));
    }
    else if (std::holds_alternative<std::string>(leftValue) && std::holds_alternative<std::string>(rightValue))
    {
        outputValue.emplace<bool>(std::get<std::string>(leftValue) != std::get<std::string>(rightValue));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
             std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 != arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) != ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

HRESULT LessThanExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "<";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 < arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) < ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

HRESULT GreaterThanExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = ">";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 > arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) > ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

HRESULT LessThanOrEqualExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "<=";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 <= arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) <= ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

HRESULT GreaterThanOrEqualExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = ">=";

    auto setError =
        [&]() -> void
        {
            FillBinaryErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    auto setAmbiguousError =
        [&]() -> void
        {
            FillBinaryAmbiguousErrorOutput(opName, leftValue, rightValue, output);
            Status = E_INVALIDARG;
        };

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<std::string>(leftValue) || std::holds_alternative<std::string>(rightValue))
    {
        setError();
    }
    else if ((std::holds_alternative<uint64_t>(leftValue) &&
                (std::holds_alternative<int8_t>(rightValue) ||
                 std::holds_alternative<int16_t>(rightValue) ||
                 std::holds_alternative<int32_t>(rightValue) ||
                 std::holds_alternative<int64_t>(rightValue))) ||
             (std::holds_alternative<uint64_t>(rightValue) &&
                (std::holds_alternative<int8_t>(leftValue) ||
                 std::holds_alternative<int16_t>(leftValue) ||
                 std::holds_alternative<int32_t>(leftValue) ||
                 std::holds_alternative<int64_t>(leftValue))))
    {
        setAmbiguousError();
    }
    else
    {
        const bool result = std::visit(
            [](auto &&arg1, auto &&arg2) -> bool
            {
                using T1 = std::decay_t<decltype(arg1)>;
                using T2 = std::decay_t<decltype(arg2)>;

                if constexpr (std::is_same_v<T1, bool> || std::is_same_v<T2, bool> ||
                              std::is_same_v<T1, std::string> || std::is_same_v<T2, std::string> ||
                              std::is_same_v<T1, std::monostate> || std::is_same_v<T2, std::monostate> ||
                              (std::is_same_v<T1, uint64_t> && (std::is_same_v<T2, int8_t> || std::is_same_v<T2, int16_t> || std::is_same_v<T2, int32_t> || std::is_same_v<T2, int64_t>)) ||
                              (std::is_same_v<T2, uint64_t> && (std::is_same_v<T1, int8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, int64_t>)))
                {
                    assert(false && "must never reach this line of code.");
                    return false;
                }
                else if constexpr (std::is_same_v<T1, float> || std::is_same_v<T2, float> ||
                                   std::is_same_v<T1, double> || std::is_same_v<T2, double> ||
                                   std::is_same_v<T1, uint64_t> || std::is_same_v<T2, uint64_t>)
                {
                    return arg1 >= arg2;
                }
                else
                {
                    return ConvertToNumeric<int64_t>(arg1) >= ConvertToNumeric<int64_t>(arg2);
                }
            }, leftValue, rightValue);

        outputValue.emplace<bool>(result);
    }

    return Status;
}

} // unnamed namespace

HRESULT CalculateBinary(Parser::SyntaxKind kind, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue,
                        PrimitiveValue &outputValue, std::string &output)
{
    assert(!std::holds_alternative<std::monostate>(leftValue) && "leftValue not properly initialized.");
    assert(!std::holds_alternative<std::monostate>(rightValue) && "rightValue not properly initialized.");

    static const std::unordered_map<Parser::SyntaxKind, std::function<HRESULT(const PrimitiveValue &, const PrimitiveValue &,
                                                                              PrimitiveValue &, std::string &)>> OperatorImplementation{
        {Parser::SyntaxKind::AddExpression, AddExpression},
        {Parser::SyntaxKind::SubtractExpression, SubtractExpression},
        {Parser::SyntaxKind::MultiplyExpression, MultiplyExpression},
        {Parser::SyntaxKind::DivideExpression, DivideExpression},
        {Parser::SyntaxKind::ModuloExpression, ModuloExpression},
        {Parser::SyntaxKind::LeftShiftExpression, LeftShiftExpression},
        {Parser::SyntaxKind::RightShiftExpression, RightShiftExpression},
        {Parser::SyntaxKind::BitwiseAndExpression, BitwiseAndExpression},
        {Parser::SyntaxKind::BitwiseOrExpression, BitwiseOrExpression},
        {Parser::SyntaxKind::ExclusiveOrExpression, ExclusiveOrExpression},
        {Parser::SyntaxKind::LogicalAndExpression, LogicalAndExpression},
        {Parser::SyntaxKind::LogicalOrExpression, LogicalOrExpression},
        {Parser::SyntaxKind::EqualsExpression, EqualsExpression},
        {Parser::SyntaxKind::NotEqualsExpression, NotEqualsExpression},
        {Parser::SyntaxKind::LessThanExpression, LessThanExpression},
        {Parser::SyntaxKind::GreaterThanExpression, GreaterThanExpression},
        {Parser::SyntaxKind::LessThanOrEqualExpression, LessThanOrEqualExpression},
        {Parser::SyntaxKind::GreaterThanOrEqualExpression, GreaterThanOrEqualExpression}
    };

    auto findOperator = OperatorImplementation.find(kind);
    if (findOperator == OperatorImplementation.end())
    {
        output = "Unknown binary operator.";
        return E_INVALIDARG;
    }

    return findOperator->second(leftValue, rightValue, outputValue, output);
}

} // namespace dncdbg::PrimitiveTypes
