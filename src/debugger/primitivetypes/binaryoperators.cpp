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

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; }; // NOLINT(misc-multiple-inheritance)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

template <typename T>
struct TypeMapping
{
    static constexpr std::string_view description = "Unknown Type";
};
template <> struct TypeMapping<bool>        { static constexpr std::string_view description = "bool"; };
template <> struct TypeMapping<WCHAR>       { static constexpr std::string_view description = "char"; };
template <> struct TypeMapping<std::string> { static constexpr std::string_view description = "string"; };
template <> struct TypeMapping<uint8_t>     { static constexpr std::string_view description = "byte"; };
template <> struct TypeMapping<uint16_t>    { static constexpr std::string_view description = "ushort"; };
template <> struct TypeMapping<uint32_t>    { static constexpr std::string_view description = "uint"; };
template <> struct TypeMapping<uint64_t>    { static constexpr std::string_view description = "ulong"; };
template <> struct TypeMapping<int8_t>      { static constexpr std::string_view description = "sbyte"; };
template <> struct TypeMapping<int16_t>     { static constexpr std::string_view description = "short"; };
template <> struct TypeMapping<int32_t>     { static constexpr std::string_view description = "int"; };
template <> struct TypeMapping<int64_t>     { static constexpr std::string_view description = "long"; };
template <> struct TypeMapping<double>      { static constexpr std::string_view description = "double"; };
template <> struct TypeMapping<float>       { static constexpr std::string_view description = "float"; };

void FillBinaryErrorOutput(const std::string_view &opName, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue, std::string &output)
{
    const std::string_view leftTypeName = std::visit(
        [](auto &arg) -> std::string_view
        {
            using ActiveType = std::decay_t<decltype(arg)>;
            return TypeMapping<ActiveType>::description;
        }, leftValue.value);

    const std::string_view rightTypeName = std::visit(
        [](auto &arg) -> std::string_view
        {
            return TypeMapping<std::decay_t<decltype(arg)>>::description;
        }, rightValue.value);

    std::ostringstream ss;
    ss << "error: Operator '" << opName << "' cannot be applied to operands of type '" << leftTypeName << "' and '" << rightTypeName << "'";
    output = ss.str();
}

// Helper template to convert PrimitiveTypeNativeValue to target numeric type.
// Handles int8_t specially by casting through uint8_t to preserve bit pattern.
template <typename TargetType>
TargetType ConvertToNumeric(const PrimitiveTypeNativeValue &value)
{
    TargetType result{};
    std::visit(overloaded {
        [](const std::monostate &) { assert(false); },
        [](const bool &) { assert(false); },
        [](const std::string &) { assert(false); },
        [&result](const int8_t &arg) { result = static_cast<TargetType>(static_cast<uint8_t>(arg)); },
        [&result](auto &arg) { result = static_cast<TargetType>(arg); }
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
        [&](const PrimitiveTypeNativeValue &value) -> std::string
        {
            std::string ret;
            std::visit(overloaded {
                [](const std::monostate &) { assert(false && "inputValue not properly initialized."); },
                [&](const bool &arg) { ret = arg ? "True" : "False"; },
                [&](const WCHAR &arg) { WSTRING tmp(2, '\0'); tmp.at(0) = arg; ret = to_utf8(tmp.c_str()); },
                [&](const std::string &arg) { ret = arg; },
                [&](const double &arg) { std::ostringstream ss; ss << arg; ret = ss.str(); },
                [&](const float &arg) { std::ostringstream ss; ss << arg; ret = ss.str(); },
                [&](auto &arg) { ret = std::to_string(arg); },
            }, value);
            return ret;
        };

    if (std::holds_alternative<std::string>(leftValue.value) || std::holds_alternative<std::string>(rightValue.value))
    {
        const std::string leftStrValue = ConvertToString(leftValue.value);
        const std::string rightStrValue = ConvertToString(rightValue.value);
        outputValue.type = ELEMENT_TYPE_STRING;
        outputValue.value.emplace<std::string>(leftStrValue + rightStrValue);
    }
    else if (std::holds_alternative<bool>(leftValue.value) || std::holds_alternative<bool>(rightValue.value) ||
             (std::holds_alternative<uint64_t>(leftValue.value) &&
                (std::holds_alternative<int8_t>(rightValue.value) ||
                 std::holds_alternative<int16_t>(rightValue.value) ||
                 std::holds_alternative<int32_t>(rightValue.value) ||
                 std::holds_alternative<int64_t>(rightValue.value))) ||
             (std::holds_alternative<uint64_t>(rightValue.value) &&
                (std::holds_alternative<int8_t>(leftValue.value) ||
                 std::holds_alternative<int16_t>(leftValue.value) ||
                 std::holds_alternative<int32_t>(leftValue.value) ||
                 std::holds_alternative<int64_t>(leftValue.value))))
    {
        setError();
    }
    else if (std::holds_alternative<double>(leftValue.value) || std::holds_alternative<double>(rightValue.value))
    {
        outputValue.type = ELEMENT_TYPE_R8;
        outputValue.value.emplace<double>(ConvertToNumeric<double>(leftValue.value) + ConvertToNumeric<double>(rightValue.value));
    }
    else if (std::holds_alternative<float>(leftValue.value) || std::holds_alternative<float>(rightValue.value))
    {
        outputValue.type = ELEMENT_TYPE_R4;
        outputValue.value.emplace<float>(ConvertToNumeric<float>(leftValue.value) + ConvertToNumeric<float>(rightValue.value));
    }
    else if (std::holds_alternative<int64_t>(leftValue.value) || std::holds_alternative<int64_t>(rightValue.value) ||
             (std::holds_alternative<uint32_t>(leftValue.value) &&
                (std::holds_alternative<int8_t>(rightValue.value) ||
                 std::holds_alternative<int16_t>(rightValue.value) ||
                 std::holds_alternative<int32_t>(rightValue.value))) ||
             (std::holds_alternative<uint32_t>(rightValue.value) &&
                (std::holds_alternative<int8_t>(leftValue.value) ||
                 std::holds_alternative<int16_t>(leftValue.value) ||
                 std::holds_alternative<int32_t>(leftValue.value))))
    {
        outputValue.type = ELEMENT_TYPE_I8;
        outputValue.value.emplace<int64_t>(ConvertToNumeric<int64_t>(leftValue.value) + ConvertToNumeric<int64_t>(rightValue.value));
    }
    else if (std::holds_alternative<uint64_t>(leftValue.value) || std::holds_alternative<uint64_t>(rightValue.value))
    {
        outputValue.type = ELEMENT_TYPE_U8;
        outputValue.value.emplace<uint64_t>(ConvertToNumeric<uint64_t>(leftValue.value) + ConvertToNumeric<uint64_t>(rightValue.value));
    }
    else if (std::holds_alternative<uint32_t>(leftValue.value) || (std::holds_alternative<uint32_t>(rightValue.value)))
    {
        outputValue.type = ELEMENT_TYPE_U4;
        outputValue.value.emplace<uint32_t>(ConvertToNumeric<uint32_t>(leftValue.value) + ConvertToNumeric<uint32_t>(rightValue.value));
    }
    else
    {
        outputValue.type = ELEMENT_TYPE_I4;
        outputValue.value.emplace<int32_t>(ConvertToNumeric<int32_t>(leftValue.value) + ConvertToNumeric<int32_t>(rightValue.value));
    }

    return Status;
}

} // unnamed namespace

HRESULT CalculateBinary(Parser::SyntaxKind kind, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue,
                        PrimitiveValue &outputValue, std::string &output)
{
    static const std::unordered_map<Parser::SyntaxKind, std::function<HRESULT(const PrimitiveValue &, const PrimitiveValue &,
                                                                              PrimitiveValue &, std::string &)>> OperatorImplementation{
        {Parser::SyntaxKind::AddExpression, AddExpression}
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
