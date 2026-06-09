// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
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

void FillErrorOutput(std::string_view opName, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, std::string &output)
{
    std::ostringstream ss;
    ss << "error: Operator '" << opName << "' cannot be applied to operands of type '" << GetManagedTypeName(leftValue)
       << "' and '" << GetManagedTypeName(rightValue) << "'";
    output = ss.str();
}

void FillAmbiguousErrorOutput(std::string_view opName, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, std::string &output)
{
    std::ostringstream ss;
    ss << "error: Operator '" << opName << "' is ambiguous on operands of type '" << GetManagedTypeName(leftValue)
       << "' and '" << GetManagedTypeName(rightValue) << "'";
    output = ss.str();
}

void FillConvertErrorOutput(const PrimitiveValue &primValue, std::string &output)
{
    std::ostringstream ss;
    ss << "error: Cannot implicitly convert type '" << GetManagedTypeName(primValue) << "' to 'bool'";
    output = ss.str();
}

// Helper template to convert PrimitiveValue to target numeric type.
template <typename TargetType>
TargetType ConvertToNumeric(const PrimitiveValue &value)
{
    TargetType result{};
    std::visit(overloaded {
        [](const std::monostate &) { assert(false && "value not properly initialized."); },
        [](const bool &) { assert(false && "value not supported."); },
        [&result](auto &arg) { result = static_cast<TargetType>(arg); } // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
    }, value);
    return result;
}

// Helper template for arithmetic operations (Add, Subtract, Multiply, Divide, Modulo)
template <typename FloatOp, typename IntOp>
HRESULT ArithmeticExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                                 std::string &output, std::string_view opName, FloatOp floatOp, IntOp intOp)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
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
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<double>(floatOp(ConvertToNumeric<double>(leftValue), ConvertToNumeric<double>(rightValue)));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<float>(floatOp(ConvertToNumeric<float>(leftValue), ConvertToNumeric<float>(rightValue)));
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
        outputValue.emplace<int64_t>(intOp(ConvertToNumeric<int64_t>(leftValue), ConvertToNumeric<int64_t>(rightValue)));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(intOp(ConvertToNumeric<uint64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue)));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(intOp(ConvertToNumeric<uint32_t>(leftValue), ConvertToNumeric<uint32_t>(rightValue)));
    }
    else
    {
        outputValue.emplace<int32_t>(intOp(ConvertToNumeric<int32_t>(leftValue), ConvertToNumeric<int32_t>(rightValue)));
    }

    return Status;
}

HRESULT AddExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ArithmeticExpressionImpl(leftValue, rightValue, outputValue, output, "+",
                                    [](const auto &left, const auto &right) { return left + right; },
                                    [](const auto &left, const auto &right) { return left + right; });
}

HRESULT SubtractExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ArithmeticExpressionImpl(leftValue, rightValue, outputValue, output, "-",
                                    [](const auto &left, const auto &right) { return left - right; },
                                    [](const auto &left, const auto &right) { return left - right; });
}

HRESULT MultiplyExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ArithmeticExpressionImpl(leftValue, rightValue, outputValue, output, "*",
                                    [](const auto &left, const auto &right) { return left * right; },
                                    [](const auto &left, const auto &right) { return left * right; });
}

HRESULT DivideExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ArithmeticExpressionImpl(leftValue, rightValue, outputValue, output, "/",
                                    [](const auto &left, const auto &right) { return left / right; },
                                    [](const auto &left, const auto &right) { return left / right; });
}

HRESULT ModuloExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ArithmeticExpressionImpl(leftValue, rightValue, outputValue, output, "%",
                                    [](const auto &left, const auto &right) { return std::fmod(left, right); },
                                    [](const auto &left, const auto &right) { return left % right; });
}

// Helper template for shift operations (Left, Right)
template <typename ShiftOp>
HRESULT ShiftExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                            std::string &output, std::string_view opName, ShiftOp shiftOp)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
        std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue) ||
        std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue) ||
                                                    std::holds_alternative<uint32_t>(rightValue) ||
                                                    std::holds_alternative<uint64_t>(rightValue) ||
                                                    std::holds_alternative<int64_t>(rightValue))
    {
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
    }
    else if (std::holds_alternative<int64_t>(leftValue))
    {
        outputValue.emplace<int64_t>(shiftOp(ConvertToNumeric<int64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint64_t>(leftValue))
    {
        outputValue.emplace<uint64_t>(shiftOp(ConvertToNumeric<uint64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue) % maxShift8Byte));
    }
    else if (std::holds_alternative<uint32_t>(leftValue))
    {
        outputValue.emplace<uint32_t>(shiftOp(ConvertToNumeric<uint32_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }
    else
    {
        outputValue.emplace<int32_t>(shiftOp(ConvertToNumeric<int32_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue) % maxShift4Byte));
    }

    return Status;
}

HRESULT LeftShiftExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ShiftExpressionImpl(leftValue, rightValue, outputValue, output, "<<",
                               [](const auto &left, const auto &right) { return left << right; });
}

HRESULT RightShiftExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ShiftExpressionImpl(leftValue, rightValue, outputValue, output, ">>",
                               [](const auto &left, const auto &right) { return left >> right; });
}

// Helper template for bitwise operations (AND, OR, XOR)
template <typename BoolOp, typename BitwiseOp>
HRESULT BitwiseExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                              std::string &output, std::string_view opName, BoolOp boolOp, BitwiseOp bitwiseOp)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(boolOp(std::get<bool>(leftValue), std::get<bool>(rightValue)));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue) ||
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
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
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
        outputValue.emplace<int64_t>(bitwiseOp(ConvertToNumeric<int64_t>(leftValue), ConvertToNumeric<int64_t>(rightValue)));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<uint64_t>(bitwiseOp(ConvertToNumeric<uint64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue)));
    }
    else if (std::holds_alternative<uint32_t>(leftValue) || (std::holds_alternative<uint32_t>(rightValue)))
    {
        outputValue.emplace<uint32_t>(bitwiseOp(ConvertToNumeric<uint32_t>(leftValue), ConvertToNumeric<uint32_t>(rightValue)));
    }
    else
    {
        outputValue.emplace<int32_t>(bitwiseOp(ConvertToNumeric<int32_t>(leftValue), ConvertToNumeric<int32_t>(rightValue)));
    }

    return Status;
}

HRESULT BitwiseAndExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return BitwiseExpressionImpl(leftValue, rightValue, outputValue, output, "&",
                                 [](bool left, bool right) { return left && right; },
                                 [](const auto &left, const auto &right) { return left & right; });
}

HRESULT BitwiseOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return BitwiseExpressionImpl(leftValue, rightValue, outputValue, output, "|",
                                 [](bool left, bool right) { return left || right; },
                                 [](const auto &left, const auto &right) { return left | right; });
}

HRESULT ExclusiveOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return BitwiseExpressionImpl(leftValue, rightValue, outputValue, output, "^",
                                 [](bool left, bool right) { return left ^ right; },
                                 [](const auto &left, const auto &right) { return left ^ right; });
}

// Helper template for logical operations (AND, OR)
template <typename LogicalOp>
HRESULT LogicalExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                              std::string &output, std::string_view opName, LogicalOp op)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(op(std::get<bool>(leftValue), std::get<bool>(rightValue)));
    }
    else if (std::holds_alternative<bool>(leftValue))
    {
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
    }
    else
    {
        FillConvertErrorOutput(leftValue, output);
        Status = E_INVALIDARG;
    }

    return Status;
}

HRESULT LogicalAndExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return LogicalExpressionImpl(leftValue, rightValue, outputValue, output, "&&",
                                 [](bool left, bool right) { return left && right; });
}

HRESULT LogicalOrExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return LogicalExpressionImpl(leftValue, rightValue, outputValue, output, "||",
                                 [](bool left, bool right) { return left || right; });
}

// Helper template for equality operations (Equals, NotEquals)
template <typename CompareOp>
HRESULT EqualityExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                               std::string &output, std::string_view opName, CompareOp compareOp)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) && std::holds_alternative<bool>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(std::get<bool>(leftValue), std::get<bool>(rightValue)));
    }
    else if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue))
    {
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
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
        FillAmbiguousErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<double>(leftValue), ConvertToNumeric<double>(rightValue)));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<float>(leftValue), ConvertToNumeric<float>(rightValue)));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<uint64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue)));
    }
    else
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<int64_t>(leftValue), ConvertToNumeric<int64_t>(rightValue)));
    }

    return Status;
}

HRESULT EqualsExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return EqualityExpressionImpl(leftValue, rightValue, outputValue, output, "==",
                                  [](const auto &left, const auto &right) { return left == right; });
}

HRESULT NotEqualsExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return EqualityExpressionImpl(leftValue, rightValue, outputValue, output, "!=",
                                  [](const auto &left, const auto &right) { return left != right; });
}

// Helper template for comparison operations (<, >, <=, >=)
template <typename CompareOp>
HRESULT ComparisonExpressionImpl(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue,
                                 std::string &output, std::string_view opName, CompareOp compareOp)
{
    HRESULT Status = S_OK;

    if (std::holds_alternative<bool>(leftValue) || std::holds_alternative<bool>(rightValue))
    {
        FillErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
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
        FillAmbiguousErrorOutput(opName, leftValue, rightValue, output);
        Status = E_INVALIDARG;
    }
    else if (std::holds_alternative<double>(leftValue) || std::holds_alternative<double>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<double>(leftValue), ConvertToNumeric<double>(rightValue)));
    }
    else if (std::holds_alternative<float>(leftValue) || std::holds_alternative<float>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<float>(leftValue), ConvertToNumeric<float>(rightValue)));
    }
    else if (std::holds_alternative<uint64_t>(leftValue) || std::holds_alternative<uint64_t>(rightValue))
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<uint64_t>(leftValue), ConvertToNumeric<uint64_t>(rightValue)));
    }
    else
    {
        outputValue.emplace<bool>(compareOp(ConvertToNumeric<int64_t>(leftValue), ConvertToNumeric<int64_t>(rightValue)));
    }

    return Status;
}

HRESULT LessThanExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ComparisonExpressionImpl(leftValue, rightValue, outputValue, output, "<",
                                    [](const auto &left, const auto &right) { return left < right; });
}

HRESULT GreaterThanExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ComparisonExpressionImpl(leftValue, rightValue, outputValue, output, ">",
                                    [](const auto &left, const auto &right) { return left > right; });
}

HRESULT LessThanOrEqualExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ComparisonExpressionImpl(leftValue, rightValue, outputValue, output, "<=",
                                    [](const auto &left, const auto &right) { return left <= right; });
}

HRESULT GreaterThanOrEqualExpression(const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, PrimitiveValue &outputValue, std::string &output)
{
    return ComparisonExpressionImpl(leftValue, rightValue, outputValue, output, ">=",
                                    [](const auto &left, const auto &right) { return left >= right; });
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
