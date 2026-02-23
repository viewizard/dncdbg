// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <condition_variable>
#include <functional>
#include <list>
#include <thread>
#include <string>

namespace dncdbg
{

class ManagedDebugger;

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

enum class CallbackQueueCall
{
    FinishWorker = 0,
    Breakpoint,
    StepComplete,
    Break,
    Exception,
    CreateProcess
};

class CallbacksQueue
{
  public:

    CallbacksQueue(ManagedDebugger &debugger)
        : m_debugger(debugger),
          m_stopEventInProcess(false),
          m_callbacksWorker{&CallbacksQueue::CallbacksWorker, this}
    {
    }
    ~CallbacksQueue();

    // Called from ManagedDebugger by protocol request (Continue/Pause).
    bool IsRunning();
    HRESULT Continue(ICorDebugProcess *pProcess);
    // Stop process and set last stopped thread. If `lastStoppedThread` not passed value from protocol, find best
    // thread.
    HRESULT Pause(ICorDebugProcess *pProcess, ThreadId lastStoppedThread);
    // Analog of "pProcess->Stop(0)" call that also care about callbacks.
    HRESULT Stop(ICorDebugProcess *pProcess);

    HRESULT ContinueProcess(ICorDebugProcess *pProcess);
    HRESULT ContinueAppDomain(ICorDebugAppDomain *pAppDomain);
    HRESULT AddCallbackToQueue(ICorDebugAppDomain *pAppDomain, const std::function<void()> &callback);
    void EmplaceBack(CallbackQueueCall Call, ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                     ICorDebugBreakpoint *pBreakpoint, CorDebugStepReason Reason, ExceptionCallbackType EventType,
                     const std::string &ExcModule = std::string{});

  private:

    ManagedDebugger &m_debugger;

    // NOTE we have one entry type for both (managed and interop) callbacks (stop events),
    //      since almost all the time we have CallbackQueue with 1 entry only, no reason complicate code.
    //      Probably in future we could reuse Reason, EventType and ExcModule fields for interop events too.
    //      Each event use its own constructor.
    struct CallbackQueueEntry
    {
        CallbackQueueCall Call;
        ToRelease<ICorDebugAppDomain> trAppDomain;
        ToRelease<ICorDebugThread> trThread;
        ToRelease<ICorDebugBreakpoint> trBreakpoint;
        CorDebugStepReason Reason = CorDebugStepReason::STEP_NORMAL; // Initial value in order to suppress static analyzer warnings.
        ExceptionCallbackType EventType = ExceptionCallbackType::FIRST_CHANCE; // Initial value in order to suppress static analyzer warnings.
        std::string ExcModule;

        CallbackQueueEntry(CallbackQueueCall call,
                           ICorDebugAppDomain *pAppDomain,
                           ICorDebugThread *pThread,
                           ICorDebugBreakpoint *pBreakpoint,
                           CorDebugStepReason reason,
                           ExceptionCallbackType eventType,
                           const std::string &excModule = "")
            : Call(call),
              trAppDomain(pAppDomain),
              trThread(pThread),
              trBreakpoint(pBreakpoint),
              Reason(reason),
              EventType(eventType),
              ExcModule(excModule)
        {}

    };

    std::mutex m_callbacksMutex;
    std::condition_variable m_callbacksCV;
    std::list<CallbackQueueEntry> m_callbacksQueue; // Make sure this one initialized before m_callbacksWorker.
    bool m_stopEventInProcess;                      // Make sure this one initialized before m_callbacksWorker.
    std::thread m_callbacksWorker;

    void CallbacksWorker();
    bool CallbacksWorkerBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint);
    bool CallbacksWorkerStepComplete(ICorDebugThread *pThread, CorDebugStepReason reason);
    bool CallbacksWorkerBreak(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread);
    bool CallbacksWorkerException(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread, ExceptionCallbackType eventType, const std::string &excModule);
    bool CallbacksWorkerCreateProcess();
    static bool HasQueuedCallbacks(ICorDebugProcess *pProcess);
};

} // namespace dncdbg
