// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_VALUEPRINT_H
#define DEBUGGER_VALUEPRINT_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evalutils.h"
#include <string>

namespace dncdbg
{

class Evaluator;
class EvalStackMachine;

HRESULT PrintValue(ICorDebugThread *pThread, Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine,
                   ICorDebugValue *pInputValue, FormatSpecifier specifier, std::string &output);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, bool &hasValue);
HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output);

} // namespace dncdbg

#endif // DEBUGGER_VALUEPRINT_H
