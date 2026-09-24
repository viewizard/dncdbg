// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _MSC_VER
#include <wtypes.h>
#endif

#include "debugger/callbacksqueue.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "debugger/frames.h"
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "protocol/dap_events.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace dncdbg::CallbacksQueue
{

namespace
{

// Note: we have one entry type for both managed and interop callbacks (stop events), since the queue
// almost always contains a single entry, so there is no reason to complicate the code. Probably in
// the future the Reason, EventType and ExcModule fields could be reused for interop events too.
// Each event uses its own constructor; some fields are unused for a given event.
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
                       std::string excModule = std::string())
        : Call(call),
          trAppDomain(pAppDomain),
          trThread(pThread),
          trBreakpoint(pBreakpoint),
          Reason(reason),
          EventType(eventType),
          ExcModule(std::move(excModule))
    {
    }
};

std::function<void()> &GetNotifyProcessCreatedCallback()
{
    static std::function<void()> notifyProcessCreatedCallback;
    return notifyProcessCreatedCallback;
}

std::mutex &GetCallbacksMutex()
{
    static std::mutex callbacksMutex;
    return callbacksMutex;
}

std::condition_variable &GetCallbacksCV()
{
    static std::condition_variable callbacksCV;
    return callbacksCV;
}

std::list<CallbackQueueEntry> &GetCallbacksQueue()
{
    static std::list<CallbackQueueEntry> callbacksQueue;
    return callbacksQueue;
}

bool &GetStopEventInProcess()
{
    static bool stopEventInProcess{false};
    return stopEventInProcess;
}

std::thread &GetCallbacksWorker()
{
    static std::thread callbacksWorker;
    return callbacksWorker;
}

bool CallbacksWorkerBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint)
{
    if (S_IGNORE == Steppers::ManagedCallbackBreakpoint(pAppDomain, pThread))
    {
        // Steppers related break (for example, async stepping internal breakpoints),
        // don't emit breakpoint stop event and continue execution.
        return false;
    }

    bool atEntry = false;
    std::vector<uint32_t> hitBreakpointIds;
    if (S_IGNORE == Breakpoints::ManagedCallbackBreakpoint(pThread, pBreakpoint, hitBreakpointIds, atEntry))
    {
        // Breakpoints related break (for example, breakpoint's condition failed or stop in non-user code
        // with enabled JMC), don't emit breakpoint stop event and continue execution.
        return false;
    }

    // At this point we stop at breakpoint, disable all steppers (we could stop at breakpoint during step).
    Steppers::DisableAll(pAppDomain);

    Threads::SetLastStoppedThread(pThread);

    const ThreadId threadId(Threads::GetId(pThread));
    const StoppedEvent event(atEntry ? StoppedEventReason::Entry : StoppedEventReason::Breakpoint, std::move(hitBreakpointIds), threadId);
    DAP::EmitStoppedEvent(event);
    return true;
}

bool CallbacksWorkerStepComplete(ICorDebugThread *pThread, CorDebugStepReason reason)
{
    if (S_IGNORE == Steppers::ManagedCallbackStepComplete(pThread, reason))
    {
        // Steppers related break (for example, filtering enabled and we need continue step),
        // don't emit stop event and continue execution.
        return false;
    }

    const ThreadId threadId(Threads::GetId(pThread));
    const StoppedEvent event(StoppedEventReason::Step, threadId);

    Threads::SetLastStoppedThread(pThread);
    DAP::EmitStoppedEvent(event);
    return true;
}

bool CallbacksWorkerBreak(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    if (S_IGNORE == Breakpoints::ManagedCallbackBreak(pThread, Threads::GetLastStoppedThreadId()))
    {
        // Break related (for example, stop at `Debugger.Break()` in non-user code with enabled JMC),
        // don't emit break stop event and continue execution.
        return false;
    }

    // At this point we stop at Break, disable all steppers (we could stop at Break during step).
    Steppers::DisableAll(pAppDomain);

    Threads::SetLastStoppedThread(pThread);
    const ThreadId threadId(Threads::GetId(pThread));

    const StoppedEvent event(StoppedEventReason::Pause, threadId);
    DAP::EmitStoppedEvent(event);
    return true;
}

bool CallbacksWorkerException(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                              ExceptionCallbackType eventType)
{
    if (S_IGNORE == Breakpoints::ManagedCallbackException(pThread, eventType))
    {
        // Exception related break (for example, catch handler or filtered thrown exception),
        // don't emit stop event and continue execution.
        return false;
    }

    // At this point we stop at exception, disable all steppers (we could stop at exception during step).
    Steppers::DisableAll(pAppDomain);

    const ThreadId threadId(Threads::GetId(pThread));
    const StoppedEvent event(StoppedEventReason::Exception, threadId);
    Threads::SetLastStoppedThread(pThread);
    DAP::EmitStoppedEvent(event);
    return true;
}

void CallbacksWorker()
{
    std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    std::list<CallbackQueueEntry> &callbacksQueue = GetCallbacksQueue();
    bool &stopEventInProcess = GetStopEventInProcess();

    while (true)
    {
        while (callbacksQueue.empty() || stopEventInProcess)
        {
            // Note, during wait() (waiting for notify_one call with an entry added to the queue), the mutex
            // will be unlocked (see std::condition_variable documentation for more info).
            GetCallbacksCV().wait(lock);
        }

        auto &c = callbacksQueue.front();

        switch (c.Call)
        {
        case CallbackQueueCall::Breakpoint:
            stopEventInProcess = CallbacksWorkerBreakpoint(c.trAppDomain, c.trThread, c.trBreakpoint);
            break;
        case CallbackQueueCall::StepComplete:
            stopEventInProcess = CallbacksWorkerStepComplete(c.trThread, c.Reason);
            break;
        case CallbackQueueCall::Break:
            stopEventInProcess = CallbacksWorkerBreak(c.trAppDomain, c.trThread);
            break;
        case CallbackQueueCall::Exception:
            stopEventInProcess = CallbacksWorkerException(c.trAppDomain, c.trThread, c.EventType);
            break;
        case CallbackQueueCall::CreateProcess:
            if (GetNotifyProcessCreatedCallback())
            {
                GetNotifyProcessCreatedCallback()();
            }
            stopEventInProcess = false;
            break;
        default:
            // FinishWorker sentinel: stop the worker. Pop the entry so the queue stays empty
            // for a possible re-initialization.
            callbacksQueue.pop_front();
            return;
        }

        ToRelease<ICorDebugAppDomain> trAppDomain(c.trAppDomain.Detach());
        callbacksQueue.pop_front();

        // Continue process execution only if no stop event was emitted and the queue is empty.
        // This is safe against a fast Continue()/AddCallbackToQueue() call from a new callback, since the
        // mutex is not unlocked here; it is unlocked only in wait(), when the worker is ready for notify_one.
        if (callbacksQueue.empty() && !stopEventInProcess)
        {
            trAppDomain->Continue(0);
        }
    }
}

bool HasQueuedCallbacks(ICorDebugProcess *pProcess)
{
    BOOL bQueued = FALSE;
    pProcess->HasQueuedCallbacks(nullptr, &bQueued);
    return bQueued == TRUE;
}

} // namespace

void Initialize(std::function<void()> notifyProcessCreatedCallback)
{
    assert(!GetCallbacksWorker().joinable()); // Initialize must not be called while the worker is running.
    GetNotifyProcessCreatedCallback() = std::move(notifyProcessCreatedCallback);
    GetCallbacksWorker() = std::thread{CallbacksWorker};
}

void Cleanup()
{
    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    GetCallbacksQueue().clear();
    GetStopEventInProcess() = false;
    GetCallbacksCV().notify_one();
}

void Shutdown()
{
    try
    {
        std::unique_lock<std::mutex> lock(GetCallbacksMutex());

        // Clear the queue and call notify_one with a FinishWorker request.
        GetCallbacksQueue().clear();
        GetCallbacksQueue().emplace_front(CallbackQueueCall::FinishWorker, nullptr, nullptr, nullptr, STEP_NORMAL,
                                          ExceptionCallbackType::FIRST_CHANCE);
        GetStopEventInProcess() = false; // force the worker to proceed even while stopped
        GetCallbacksCV().notify_one();   // notify_one with lock
        lock.unlock();
        GetCallbacksWorker().join();
        GetNotifyProcessCreatedCallback() = {};
    }
    catch (...)
    {
        // We can't leave this thread running and can't finish it safely.
        // Terminate the debugger; don't allow a new debug session to start.
        std::terminate();
    }
}

HRESULT AddCallbackToQueue(ICorDebugAppDomain *pAppDomain, const std::function<void()> &callback)
{
    if (EvalWaiter::IsEvalRunning())
    {
        pAppDomain->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    callback();
    assert(!GetCallbacksQueue().empty());

    // Note, we don't check whether the queue is empty here, since callback() must add an entry to the queue.
    ToRelease<ICorDebugProcess> trProcess;
    if (SUCCEEDED(pAppDomain->GetProcess(&trProcess)) && HasQueuedCallbacks(trProcess))
    {
        pAppDomain->Continue(0);
    }
    else
    {
        GetCallbacksCV().notify_one(); // notify_one with lock
    }

    return S_OK;
}

HRESULT ContinueAppDomain(ICorDebugAppDomain *pAppDomain)
{
    if (EvalWaiter::IsEvalRunning())
    {
        if (pAppDomain == nullptr)
        {
            return E_NOTIMPL;
        }

        pAppDomain->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    ToRelease<ICorDebugProcess> trProcess;
    if (GetCallbacksQueue().empty() ||
        ((pAppDomain != nullptr) && SUCCEEDED(pAppDomain->GetProcess(&trProcess)) && HasQueuedCallbacks(trProcess)))
    {
        if (pAppDomain == nullptr)
        {
            return E_NOTIMPL;
        }
        pAppDomain->Continue(0);
    }
    else
    {
        GetCallbacksCV().notify_one(); // notify_one with lock
    }

    return S_OK;
}

HRESULT ContinueProcess(ICorDebugProcess *pProcess)
{
    if (EvalWaiter::IsEvalRunning())
    {
        if (pProcess == nullptr)
        {
            return E_NOTIMPL;
        }

        pProcess->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    if (GetCallbacksQueue().empty() || ((pProcess != nullptr) && HasQueuedCallbacks(pProcess)))
    {
        if (pProcess == nullptr)
        {
            return E_NOTIMPL;
        }

        pProcess->Continue(0);
    }
    else
    {
        GetCallbacksCV().notify_one(); // notify_one with lock
    }

    return S_OK;
}

bool IsRunning()
{
    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());
    return !GetStopEventInProcess();
}

HRESULT Continue(ICorDebugProcess *pProcess, ThreadId threadId, bool singleThread)
{
    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    bool &stopEventInProcess = GetStopEventInProcess();
    assert(stopEventInProcess);
    stopEventInProcess = false;

    if (!GetCallbacksQueue().empty())
    {
        GetCallbacksCV().notify_one(); // notify_one with lock
        return S_OK;
    }

    // Note, we must set the debug state of every thread on each continue: a previous
    // single-thread continue/step may have left some threads in THREAD_SUSPEND, so the
    // all-thread case (singleThread == false) restores them to THREAD_RUN.
    HRESULT Status = S_OK;
    ToRelease<ICorDebugThreadEnum> trThreadEnum;
    if (FAILED(Status = pProcess->EnumerateThreads(&trThreadEnum)))
    {
        stopEventInProcess = true;
        return Status;
    }
    ULONG fetched = 0;
    ToRelease<ICorDebugThread> trThread;
    const int iThreadId = static_cast<int>(threadId);
    const CorDebugThreadState state = singleThread ? THREAD_SUSPEND : THREAD_RUN;
    while (SUCCEEDED(trThreadEnum->Next(1, &trThread, &fetched)) && fetched == 1)
    {
        DWORD tid = 0;
        if (FAILED(Status = trThread->GetID(&tid)))
        {
            LOGW(log << "ICorDebugThread::GetID() call failed, target threadId=" << iThreadId);
            stopEventInProcess = true;
            return Status;
        }
        // Run the target thread; suspend all others only in the single-thread case.
        if (FAILED(Status = trThread->SetDebugState(iThreadId == static_cast<int>(tid) ? THREAD_RUN : state)))
        {
            LOGW(log << "ICorDebugThread::SetDebugState() call failed, threadId=" << static_cast<int>(tid)
                     << ", target threadId=" << iThreadId);
            stopEventInProcess = true;
            return Status;
        }
        trThread.Free();
    }

    return pProcess->Continue(0);
}

// Stop process and set last stopped thread.
HRESULT Pause(ICorDebugProcess *pProcess, ThreadId lastStoppedThread)
{
    // Must be real thread ID or ThreadId::AllThreads.
    if (!lastStoppedThread)
    {
        return E_INVALIDARG;
    }

    const std::unique_lock<std::mutex> lock(GetCallbacksMutex());

    bool &stopEventInProcess = GetStopEventInProcess();

    if (stopEventInProcess)
    {
        return S_OK; // Already stopped.
    }

    HRESULT Status = S_OK;
    // Note, if Stop() fails, no stop event will be emitted, so don't set stopEventInProcess to true in this case.
    IfFailRet(pProcess->Stop(0));
    stopEventInProcess = true;

    // Same logic as provided by vsdbg in case of pause during stepping.
    Steppers::DisableAll(pProcess);

    std::vector<Thread> threads;
    Threads::GetThreads(threads);

    // In case of DAP, command provides "pause" thread id.
    const auto lastStoppedIt = std::find_if(threads.begin(), threads.end(),
                                            [&](const Thread &t) { return t.id == lastStoppedThread; });
    if (lastStoppedIt != threads.end())
    {
        // Reorder threads so that the last stopped thread is checked first.
        std::swap(threads.front(), *lastStoppedIt);

        // Now get the stack trace for each thread and find a frame with a valid user source location.
        for (const Thread &thread : threads)
        {
            std::vector<StackFrame> stackFrames;
            ToRelease<ICorDebugThread> trThread;
            if (FAILED(pProcess->GetThread(static_cast<int>(thread.id), &trThread)) ||
                FAILED(GetStackFrames(trThread, thread.id, FrameLevel(0), 0, stackFrames)))
            {
                continue;
            }

            for (const StackFrame &stackFrame : stackFrames)
            {
                if (stackFrame.source.IsNull())
                {
                    continue;
                }

                Threads::SetLastStoppedThread(pProcess, thread.id);
                return S_OK;
            }
        }

        // DAP event must provide a thread (VS Code IDE counts on this), even if this thread doesn't have user code.
        Threads::SetLastStoppedThread(pProcess, lastStoppedThread);
        return S_OK;
    }

    // Fatal error during stop (command provides wrong thread id), just fail Pause request and don't stop process.
    stopEventInProcess = false;
    IfFailRet(pProcess->Continue(0));
    return E_FAIL;
}

// Note: caller must hold the callbacks mutex
void EmplaceBack(CallbackQueueCall Call, ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                 ICorDebugBreakpoint *pBreakpoint, CorDebugStepReason Reason, ExceptionCallbackType EventType)
{
    GetCallbacksQueue().emplace_back(Call, pAppDomain, pThread, pBreakpoint, Reason, EventType);
}

} // namespace dncdbg::CallbacksQueue
