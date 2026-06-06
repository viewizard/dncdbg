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
#include <variant>

namespace dncdbg
{

class EvalHelpers;

} // namespace dncdbg

namespace dncdbg::PrimitiveTypes
{

using PrimitiveTypeNativeValue = std::variant<std::monostate, bool, uint8_t, int8_t, uint16_t, int16_t, uint32_t,
                                              int32_t, uint64_t, int64_t, float, double, WCHAR, std::string>;
struct PrimitiveValue
{
    CorElementType type = ELEMENT_TYPE_MAX;
    PrimitiveTypeNativeValue value = std::monostate{};
};

bool IsPrimitiveType(CorElementType elemType);
HRESULT GetOperandData(ICorDebugValue *pValue, CorElementType elemType, PrimitiveValue &primValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, CorElementType elemType, void *ptr, ICorDebugValue **ppValue);
HRESULT CreateICorValue(ICorDebugThread *pThread, EvalHelpers *pEvalHelpers, PrimitiveValue &primValue, ICorDebugValue **ppValue);

HRESULT CalculateUnary(Parser::SyntaxKind kind, const PrimitiveValue &inputValue, PrimitiveValue &outputValue, std::string &output);

} // namespace dncdbg::PrimitiveTypes

#endif // DEBUGGER_PRIMITIVETYPES_TYPES_H
