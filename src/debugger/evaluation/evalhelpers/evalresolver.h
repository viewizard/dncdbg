// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_EVALRESOLVER_H
#define DEBUGGER_EVALUATION_EVALHELPERS_EVALRESOLVER_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evaluation/walkers/types.h"
#include "types/types.h"
#include <memory>
#include <string>
#include <vector>

namespace dncdbg::EvalResolver
{

// Resolve identifiers against the current frame: stack variables, "this" and its members,
// pseudo-variables ($exception, $pid, $tid), and statics of nested classes.
// Optionally returns the `editable` state and, if the result is a property, setter-related information.
HRESULT ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pForcedThisValue,
                           Walkers::SetterData *pInputSetterData, std::vector<std::string> &identifiers,
                           FormatSpecifier specifier, ICorDebugValue **ppResultValue, std::string *pRealDisplayTypeName,
                           std::unique_ptr<Walkers::SetterData> *pResultSetterData, ICorDebugType **ppResultType);

} // namespace dncdbg::EvalResolver

#endif // DEBUGGER_EVALUATION_EVALHELPERS_EVALRESOLVER_H
