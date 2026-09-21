// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <vector>

// Facade for the breakpoints functionality. Hides all breakpoint-related implementation
// details (BreakBreakpoint, EntryBreakpoint, ExceptionBreakpoints, FunctionBreakpoints,
// SourceBreakpoints, etc.) from the rest of the debugger code.

namespace dncdbg::Breakpoints
{

void SetJustMyCode(bool enable);
void SetLastStoppedIlOffset(ICorDebugProcess *pProcess, const ThreadId &lastStoppedThreadId);
void SetStopAtEntry(bool enable);
void Cleanup();
HRESULT DisableAll(ICorDebugProcess *pProcess);

HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
HRESULT SetSourceBreakpoints(bool haveProcess, const Source &source, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints);
HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints, std::vector<Breakpoint> &breakpoints);

HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo);

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount();
#endif // DEBUG_INTERNAL_TESTS

// Important! Callback-related methods must control the return of succeeded return codes.
// Do not allow debugger API to return succeeded (uncontrolled) return codes.
// Bad :
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId);
HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                  std::vector<uint32_t> &hitBreakpointIds, bool &atEntry);
HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType);
HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);
HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread);

} // namespace dncdbg::Breakpoints

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_H
