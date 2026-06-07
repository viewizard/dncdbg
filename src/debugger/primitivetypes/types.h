// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_PRIMITIVETYPES_TYPES_H
#define DEBUGGER_PRIMITIVETYPES_TYPES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "expressionparser/parser.h"
#include "utils/utf.h"
#include <string>
#include <string_view>
#include <variant>

namespace dncdbg
{

class EvalHelpers;

} // namespace dncdbg

namespace dncdbg::PrimitiveTypes
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

using PrimitiveValue = std::variant<std::monostate, bool, uint8_t, int8_t, uint16_t, int16_t, uint32_t,
                                    int32_t, uint64_t, int64_t, float, double, WCHAR, std::string>;

bool IsPrimitiveType(CorElementType elemType);
HRESULT GetOperandData(ICorDebugValue *pValue, CorElementType elemType, PrimitiveValue &primValue);
CorElementType GetCorElementType(const PrimitiveValue &primValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, CorElementType elemType, void *ptr, ICorDebugValue **ppValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, EvalHelpers *pEvalHelpers, PrimitiveValue &primValue, ICorDebugValue **ppValue);

HRESULT CalculateUnary(Parser::SyntaxKind kind, const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output);
HRESULT CalculateBinary(Parser::SyntaxKind kind, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue,
                        PrimitiveValue &outputValue, std::string &output);

} // namespace dncdbg::PrimitiveTypes

#endif // DEBUGGER_PRIMITIVETYPES_TYPES_H
