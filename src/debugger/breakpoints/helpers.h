// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_HELPERS_H
#define DEBUGGER_BREAKPOINTS_HELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string>
#include <vector>

namespace dncdbg
{

class Evaluator;
class EvalStackMachine;

namespace BreakpointHelpers
{

HRESULT IsSameFunctionBreakpoint(ICorDebugFunctionBreakpoint *pBreakpoint1, ICorDebugFunctionBreakpoint *pBreakpoint2);
HRESULT GetFunctionBreakpointModAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &modAddress);
HRESULT IsEnableByCondition(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread,
                            const std::string &condition, std::string &output);
HRESULT SkipBreakpoint(ICorDebugModule *pModule, mdMethodDef methodToken, bool justMyCode);
HRESULT GetBreakpointNativeAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &nativeAddress);

} // namespace BreakpointHelpers

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_HELPERS_H
