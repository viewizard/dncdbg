// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/primitivetypes/types.h"
#include <cassert>
#include <functional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace dncdbg::PrimitiveTypes
{

namespace
{

void FillUnaryErrorOutput(const std::string_view &opName, const std::string_view &type, std::string &output)
{
    std::ostringstream ss;
    ss << "error: Operator '" << opName << "' cannot be applied to operand of type '" << type << "'";
    output = ss.str();
}

HRESULT UnaryPlusExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "+";

    auto convertToInt32 =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<int32_t>(arg);
        };

    auto preserveType =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<std::decay_t<decltype(arg)>>(arg);
        };

    auto setError =
        [&](auto &&arg) -> void
        {
            FillUnaryErrorOutput(opName, TypeMapping<std::decay_t<decltype(arg)>>::description, output);
            Status = E_INVALIDARG;
        };

    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "inputValue not properly initialized."); },
        [&](const bool &arg) { setError(arg); },
        [&](const WCHAR &arg) { convertToInt32(arg); },
        [&](const std::string &arg) { setError(arg); },
        [&](const uint8_t &arg) { convertToInt32(arg); },
        [&](const uint16_t &arg) { convertToInt32(arg); },
        [&](const uint32_t &arg) { preserveType(arg); },
        [&](const uint64_t &arg) { preserveType(arg); },
        [&](const int8_t &arg) { convertToInt32(arg); },
        [&](const int16_t &arg) { convertToInt32(arg); },
        [&](const int32_t &arg) { preserveType(arg); },
        [&](const int64_t &arg) { preserveType(arg); },
        [&](const double &arg) { preserveType(arg); },
        [&](const float &arg) { preserveType(arg); }
    }, inputValue);

    return Status;
}

HRESULT UnaryMinusExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "-";

    auto convertToNegativeInt32 =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<int32_t>(-1 * static_cast<int32_t>(arg));
        };

    auto convertToNegativeInt64 =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<int64_t>(-1 * static_cast<int64_t>(arg));
        };

    auto preserveNegativeType =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<std::decay_t<decltype(arg)>>(-1 * arg);
        };

    auto setError =
        [&](auto &&arg) -> void
        {
            FillUnaryErrorOutput(opName, TypeMapping<std::decay_t<decltype(arg)>>::description, output);
            Status = E_INVALIDARG;
        };

    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "inputValue not properly initialized."); },
        [&](const bool &arg) { setError(arg); },
        [&](const WCHAR &arg) { convertToNegativeInt32(arg); },
        [&](const std::string &arg) { setError(arg); },
        [&](const uint8_t &arg) { convertToNegativeInt32(arg); },
        [&](const uint16_t &arg) { convertToNegativeInt32(arg); },
        [&](const uint32_t &arg) { convertToNegativeInt64(arg); },
        [&](const uint64_t &arg) { setError(arg); },
        [&](const int8_t &arg) { convertToNegativeInt32(arg); },
        [&](const int16_t &arg) { convertToNegativeInt32(arg); },
        [&](const int32_t &arg) { preserveNegativeType(arg); },
        [&](const int64_t &arg) { preserveNegativeType(arg); },
        [&](const double &arg) { preserveNegativeType(arg); },
        [&](const float &arg) { preserveNegativeType(arg); }
    }, inputValue);

    return Status;
}

HRESULT BitwiseNotExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "~";

    auto convertToInvertInt32 =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<int32_t>(~static_cast<int32_t>(arg));
        };

    auto preserveInvertType =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<std::decay_t<decltype(arg)>>(~arg);
        };

    auto setError =
        [&](auto &&arg) -> void
        {
            FillUnaryErrorOutput(opName, TypeMapping<std::decay_t<decltype(arg)>>::description, output);
            Status = E_INVALIDARG;
        };

    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "inputValue not properly initialized."); },
        [&](const bool &arg) { setError(arg); },
        [&](const WCHAR &arg) { convertToInvertInt32(arg); },
        [&](const std::string &arg) { setError(arg); },
        [&](const uint8_t &arg) { convertToInvertInt32(arg); },
        [&](const uint16_t &arg) { convertToInvertInt32(arg); },
        [&](const uint32_t &arg) { preserveInvertType(arg); },
        [&](const uint64_t &arg) { preserveInvertType(arg); },
        [&](const int8_t &arg) { convertToInvertInt32(arg); },
        [&](const int16_t &arg) { convertToInvertInt32(arg); },
        [&](const int32_t &arg) { convertToInvertInt32(arg); },
        [&](const int64_t &arg) { preserveInvertType(arg); },
        [&](const double &arg) { setError(arg); },
        [&](const float &arg) { setError(arg); }
    }, inputValue);

    return Status;
}

HRESULT LogicalNotExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    HRESULT Status = S_OK;
    static constexpr std::string_view opName = "!";

    auto preserveNotType =
        [&](auto &&arg) -> void
        {
            outputValue.emplace<std::decay_t<decltype(arg)>>(!arg);
        };

    auto setError =
        [&](auto &&arg) -> void
        {
            FillUnaryErrorOutput(opName, TypeMapping<std::decay_t<decltype(arg)>>::description, output);
            Status = E_INVALIDARG;
        };

    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "inputValue not properly initialized."); },
        [&](const bool &arg) { preserveNotType(arg); },
        [&](const WCHAR &arg) { setError(arg); },
        [&](const std::string &arg) { setError(arg); },
        [&](const uint8_t &arg) { setError(arg); },
        [&](const uint16_t &arg) { setError(arg); },
        [&](const uint32_t &arg) { setError(arg); },
        [&](const uint64_t &arg) { setError(arg); },
        [&](const int8_t &arg) { setError(arg); },
        [&](const int16_t &arg) { setError(arg); },
        [&](const int32_t &arg) { setError(arg); },
        [&](const int64_t &arg) { setError(arg); },
        [&](const double &arg) { setError(arg); },
        [&](const float &arg) { setError(arg); }
    }, inputValue);

    return Status;
}

} // unnamed namespace

HRESULT CalculateUnary(Parser::SyntaxKind kind, const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    assert(!std::holds_alternative<std::monostate>(inputValue) && "inputValue not properly initialized.");

    static const std::unordered_map<Parser::SyntaxKind, std::function<HRESULT(const PrimitiveValue &, PrimitiveValue &, std::string &)>> OperatorImplementation{
        {Parser::SyntaxKind::UnaryPlusExpression, UnaryPlusExpression},
        {Parser::SyntaxKind::UnaryMinusExpression, UnaryMinusExpression},
        {Parser::SyntaxKind::BitwiseNotExpression, BitwiseNotExpression},
        {Parser::SyntaxKind::LogicalNotExpression, LogicalNotExpression}
    };

    auto findOperator = OperatorImplementation.find(kind);
    if (findOperator == OperatorImplementation.end())
    {
        output = "Unknown unary operator.";
        return E_INVALIDARG;
    }

    return findOperator->second(inputValue, outputValue, output);
}

} // namespace dncdbg::PrimitiveTypes
