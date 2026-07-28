// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALHELPERS_H
#define DEBUGGER_EVALHELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "utils/utf.h"
#include <string>
#include <type_traits>
#include <vector>

namespace dncdbg
{

class Evaluator;
class EvalStackMachine;

HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull = nullptr);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, bool &hasValue);
void ParseFormatSpecifier(const std::string &expressionWithFormat, std::string &expression, FormatSpecifier &specifier);
HRESULT FindFunctionInModule(ICorDebugThread *pThread, const std::string &moduleFileName, const WSTRING &typeName,
                             const WSTRING &methodName, ICorDebugFunction **ppFunction);
void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts);
void BuildTextWithEval(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread, ICorDebugValue *pForcedThisValue,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output);

} // namespace dncdbg

#endif // DEBUGGER_EVALHELPERS_H
