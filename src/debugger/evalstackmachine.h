// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALSTACKMACHINE_H
#define DEBUGGER_EVALSTACKMACHINE_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evaluator.h"
#include "types/types.h"
#include <memory>
#include <string>

namespace dncdbg::EvalStackMachine
{

// Evaluate an expression. Optionally returns the `editable` state and, if the result is a property, setter-related information.
HRESULT EvaluateExpression(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &expression, FormatSpecifier specifier,
                           ICorDebugValue *pForcedThisValue, ICorDebugValue **ppResultValue, std::string *pRealDisplayTypeName,
                           std::string &output, bool *pEditable = nullptr, std::unique_ptr<Evaluator::SetterData> *pResultSetterData = nullptr);

// Set the value of pValue from an expression, implicitly casting the expression result to the type of pValue if needed.
HRESULT SetValueByExpression(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                             const std::string &expression, std::string &output);

} // namespace dncdbg::EvalStackMachine

#endif // DEBUGGER_EVALSTACKMACHINE_H
