// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <functional>
#include <string>
#include <vector>

namespace dncdbg::FunctionBreakpoints
{

HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);

// Important! Must provide succeeded return code:
// S_OK - breakpoint hit
// S_FALSE - no breakpoint hit
HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                           std::vector<uint32_t> &hitBreakpointIds);

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount();
#endif // DEBUG_INTERNAL_TESTS

// Important! Callback-related methods must control the return of succeeded return codes.
// Do not allow debugger API to return succeeded (uncontrolled) return codes.
// Bad:
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

// Cleans up the FunctionBreakpoints internal state. See Breakpoints::Cleanup().
void Cleanup();

} // namespace dncdbg::FunctionBreakpoints

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H
