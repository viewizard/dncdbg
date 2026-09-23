// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINT_BREAK_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINT_BREAK_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"

namespace dncdbg::BreakBreakpoint
{

void SetLastStoppedIlOffset(ICorDebugThread *pThread);

// Important! Callbacks related methods must control return for succeeded return code.
// Do not allow debugger API return succeeded (uncontrolled) return code.
// Bad :
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId);

// Cleans up the BreakBreakpoint internal state. See Breakpoints::Cleanup().
void Cleanup();

} // namespace dncdbg::BreakBreakpoint

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINT_BREAK_H
