// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_CALLBACKSQUEUE_H
#define DEBUGGER_CALLBACKSQUEUE_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <functional>

namespace dncdbg::CallbacksQueue
{

// https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/icordebugcontroller-hasqueuedcallbacks-method
//
// Callbacks will be dispatched one at a time, each time ICorDebugController::Continue is called.
// The debugger can check this flag if it wants to report multiple debugging events that occur simultaneously.
//
// When debugging events are queued, they have already occurred, so the debugger must drain the entire queue
// to be sure of the state of the debuggee. (Call ICorDebugController::Continue to drain the queue.) For example,
// if the queue contains two debugging events on thread X, and the debugger suspends thread X after the first debugging
// event and then calls ICorDebugController::Continue, the second debugging event for thread X will be dispatched
// although the thread has been suspended.

enum class CallbackQueueCall : uint8_t
{
    FinishWorker = 0,
    Breakpoint,
    StepComplete,
    Break,
    Exception,
    CreateProcess
};

// Initializes the callback queue and starts the worker thread. Must be called once per debugger lifetime.
void Initialize(std::function<void()> notifyProcessCreatedCallback);
// Clears queued callbacks between debug sessions, keeping the worker available for the next session.
void Cleanup();
// Stops the worker thread; must be called before the debugger state is shut down.
void Shutdown();

// Called from ManagedDebugger by protocol request (Continue/Pause).
bool IsRunning();
HRESULT Continue(ICorDebugProcess *pProcess, ThreadId threadId, bool singleThread);
// Stop the process and set the last stopped thread. If `lastStoppedThread` is not passed from the protocol, find the best thread.
HRESULT Pause(ICorDebugProcess *pProcess, ThreadId lastStoppedThread);

HRESULT ContinueProcess(ICorDebugProcess *pProcess);
HRESULT ContinueAppDomain(ICorDebugAppDomain *pAppDomain);
HRESULT AddCallbackToQueue(ICorDebugAppDomain *pAppDomain, const std::function<void()> &callback);
void EmplaceBack(CallbackQueueCall Call, ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                 ICorDebugBreakpoint *pBreakpoint, CorDebugStepReason Reason, ExceptionCallbackType EventType);

} // namespace dncdbg::CallbacksQueue

#endif // DEBUGGER_CALLBACKSQUEUE_H
