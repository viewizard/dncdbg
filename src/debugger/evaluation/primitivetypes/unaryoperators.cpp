// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include <cassert>
#include <functional>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace dncdbg::PrimitiveTypes
{

namespace
{

void FillErrorOutput(std::string_view opName, const PrimitiveValue &primValue, std::string &output)
{
    std::ostringstream ss;
    ss << "error: Operator '" << opName << "' cannot be applied to operand of type '" << GetManagedTypeName(primValue) << "'";
    output = ss.str();
}

template <typename OpFunc>
HRESULT ExecuteUnaryExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output,
                               std::string_view opName, const OpFunc &opFunc)
{
    return std::visit(overloaded {
        [&](const std::monostate &arg) -> HRESULT
        {
            assert(false && "inputValue not properly initialized.");
            FillErrorOutput(opName, arg, output);
            return E_INVALIDARG;
        },
        [&](const std::string &arg) -> HRESULT
        {
            FillErrorOutput(opName, arg, output);
            return E_INVALIDARG;
        },
        [&](auto arg) -> HRESULT
        {
            if (auto result = opFunc(arg))
            {
                outputValue = std::move(*result);
                return S_OK;
            }
            FillErrorOutput(opName, arg, output);
            return E_INVALIDARG;
        }
    }, inputValue);
}

HRESULT LogicalNotExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    return ExecuteUnaryExpression(inputValue, outputValue, output, "!",
        [](auto arg) -> std::optional<PrimitiveValue>
        {
            using T = decltype(arg);
            if constexpr (std::is_same_v<T, bool>)
            {
                return !arg;
            }
            return std::nullopt;
        });
}

HRESULT BitwiseNotExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    return ExecuteUnaryExpression(inputValue, outputValue, output, "~",
        [](auto arg) -> std::optional<PrimitiveValue>
        {
            using T = decltype(arg);

            if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
            {
                return ~arg;
            }
            else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                return ~static_cast<int32_t>(arg);
            }
            return std::nullopt;
        });
}

HRESULT UnaryMinusExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    return ExecuteUnaryExpression(inputValue, outputValue, output, "-",
        [](auto arg) -> std::optional<PrimitiveValue>
        {
            using T = decltype(arg);

            if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, float>   || std::is_same_v<T, double>)
            {
                return -arg;
            }
            else if constexpr (std::is_same_v<T, uint32_t>)
            {
                return -static_cast<int64_t>(arg);
            }
            else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, uint64_t>)
            {
                return -static_cast<int32_t>(arg);
            }
            return std::nullopt;
        });
}

HRESULT UnaryPlusExpression(const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output)
{
    return ExecuteUnaryExpression(inputValue, outputValue, output, "+",
        [](auto arg) -> std::optional<PrimitiveValue>
        {
            using T = decltype(arg);

            if constexpr (std::is_same_v<T, int8_t>  || std::is_same_v<T, uint8_t> ||
                          std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> ||
                          std::is_same_v<T, WCHAR>)
            {
                return static_cast<int32_t>(arg);
            }
            else if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
            {
                return arg;
            }
            return std::nullopt;
        });
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
