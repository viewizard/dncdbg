// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H

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

namespace dncdbg::ExceptionBreakpoints
{

HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);
HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo);

// Important! Callbacks related methods must control return for succeeded return code.
// Do not allow debugger API return succeeded (uncontrolled) return code.
// Bad :
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType);
HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread);

// Cleans up the ExceptionBreakpoints internal state. See Breakpoints::Cleanup().
void Cleanup();

} // namespace dncdbg::ExceptionBreakpoints

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H
