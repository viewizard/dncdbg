// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include "utils/hresult.h"
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
    return std::visit([&](const auto &arg) -> HRESULT
    {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>)
        {
            assert(false && "inputValue not properly initialized.");
            FillErrorOutput(opName, arg, output);
            return E_INVALIDARG;
        }
        else
        {
            if (auto result = opFunc(arg))
            {
                outputValue = *result;
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

HRESULT CalculateUnary(Parser::SyntaxKind kind, ICorDebugThread *pThread, ICorDebugValue *pInputValue,
                       ICorDebugValue **ppResultValue, std::string &output)
{
    HRESULT Status = S_OK;
    PrimitiveValue inputValue;
    IfFailRet(GetPrimitiveData(pInputValue, inputValue));

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

    PrimitiveValue outputValue;
    IfFailRet(findOperator->second(inputValue, outputValue, output));
    return CreateICorValue(pThread, outputValue, ppResultValue);
}

} // namespace dncdbg::PrimitiveTypes
