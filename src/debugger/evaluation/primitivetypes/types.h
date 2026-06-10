// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_PRIMITIVETYPES_TYPES_H
#define DEBUGGER_EVALUATION_PRIMITIVETYPES_TYPES_H

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

namespace dncdbg::PrimitiveTypes
{

using PrimitiveValue = std::variant<std::monostate, bool, uint8_t, int8_t, uint16_t, int16_t,
                                    uint32_t, int32_t, uint64_t, int64_t, float, double, WCHAR>;

bool IsPrimitiveType(CorElementType elemType);
HRESULT GetOperandData(ICorDebugValue *pValue, CorElementType elemType, PrimitiveValue &primValue);
CorElementType GetCorElementType(const PrimitiveValue &primValue);
std::string_view GetManagedTypeName(const PrimitiveValue &primValue);
std::string ToString(const PrimitiveValue &primValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, CorElementType elemType, void *ptr, ICorDebugValue **ppValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, PrimitiveValue &primValue, ICorDebugValue **ppValue);

HRESULT CalculateUnary(Parser::SyntaxKind kind, const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output);
HRESULT CalculateBinary(Parser::SyntaxKind kind, const PrimitiveValue &leftValue, const PrimitiveValue &rightValue,
                        PrimitiveValue &outputValue, std::string &output);

HRESULT ForceCastToUint(ICorDebugValue *pInputValue, uint32_t &number);

} // namespace dncdbg::PrimitiveTypes

#endif // DEBUGGER_EVALUATION_PRIMITIVETYPES_TYPES_H
