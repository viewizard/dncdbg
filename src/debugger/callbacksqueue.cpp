// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _MSC_VER
#include <wtypes.h>
#endif

#include "debugger/callbacksqueue.h"
#include "debugger/manageddebugger.h"
#include "debugger/breakpoints/breakpoints.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalwaiter.h" // NOLINT(misc-include-cleaner)
#include "debugger/steppers/steppers.h"
#include "debugger/threads.h"
#include "protocol/dapio.h"
#include <algorithm>

namespace dncdbg
{

bool CallbacksQueue::CallbacksWorkerBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint)
{
    if (S_IGNORE == m_debugger.m_uniqueSteppers->ManagedCallbackBreakpoint(pAppDomain, pThread))
    {
        // Steppers related break (for example, async stepping internal breakpoints),
        // don't emit breakpoint stop event and continue execution.
        return false;
    }

    bool atEntry = false;
    std::vector<BreakpointEvent> bpChangeEvents;
    std::vector<uint32_t> hitBreakpointIds;
    if (S_IGNORE == m_debugger.m_sharedBreakpoints->ManagedCallbackBreakpoint(pThread, pBreakpoint, hitBreakpointIds, bpChangeEvents, atEntry))
    {
        // Breakpoints related break (for example, breakpoint's condition failed or stop in non-user code
        // with enabled JMC), don't emit breakpoint stop event and continue execution.
        return false;
    }

    // At this point we stop at breakpoint, disable all steppers (we could stop at breakpoint during step).
    m_debugger.m_uniqueSteppers->DisableAllSteppers(pAppDomain);

    m_debugger.SetLastStoppedThread(pThread);
    for (const BreakpointEvent &changeEvent : bpChangeEvents)
    {
        DAPIO::EmitOutputEvent({OutputCategory::StdErr, changeEvent.breakpoint.message});
        DAPIO::EmitBreakpointEvent(changeEvent);
    }

    const ThreadId threadId(getThreadId(pThread));
    const StoppedEvent event(atEntry ? StoppedEventReason::Entry : StoppedEventReason::Breakpoint, std::move(hitBreakpointIds), threadId);
    DAPIO::EmitStoppedEvent(event);
    return true;
}

bool CallbacksQueue::CallbacksWorkerStepComplete(ICorDebugThread *pThread, CorDebugStepReason reason)
{
    if (S_IGNORE == m_debugger.m_uniqueSteppers->ManagedCallbackStepComplete(pThread, reason))
    {
        // Steppers related break (for example, filtering enabled and we need continue step),
        // don't emit stop event and continue execution.
        return false;
    }

    const ThreadId threadId(getThreadId(pThread));
    const StoppedEvent event(StoppedEventReason::Step, threadId);

    m_debugger.SetLastStoppedThread(pThread);
    DAPIO::EmitStoppedEvent(event);
    return true;
}

bool CallbacksQueue::CallbacksWorkerBreak(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    if (S_IGNORE == m_debugger.m_sharedBreakpoints->ManagedCallbackBreak(pThread, m_debugger.GetLastStoppedThreadId()))
    {
        // Break related (for example, stop at `Debugger.Break()` in non-user code with enabled JMC),
        // don't emit break stop event and continue execution.
        return false;
    }

    // At this point we stop at Break, disable all steppers (we could stop at Break during step).
    m_debugger.m_uniqueSteppers->DisableAllSteppers(pAppDomain);

    m_debugger.SetLastStoppedThread(pThread);
    const ThreadId threadId(getThreadId(pThread));

    const StoppedEvent event(StoppedEventReason::Pause, threadId);
    DAPIO::EmitStoppedEvent(event);
    return true;
}

bool CallbacksQueue::CallbacksWorkerException(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                              ExceptionCallbackType eventType, const std::string &excModule)
{
    if (S_IGNORE == m_debugger.m_sharedBreakpoints->ManagedCallbackException(pThread, eventType, excModule))
    {
        // Exception related break (for example, catch handler or filtered thrown exception),
        // don't emit stop event and continue execution.
        return false;
    }

    // At this point we stop at exception, disable all steppers (we could stop at exception during step).
    m_debugger.m_uniqueSteppers->DisableAllSteppers(pAppDomain);

    const ThreadId threadId(getThreadId(pThread));
    const StoppedEvent event(StoppedEventReason::Exception, threadId);
    m_debugger.SetLastStoppedThread(pThread);
    DAPIO::EmitStoppedEvent(event);
    return true;
}

bool CallbacksQueue::CallbacksWorkerCreateProcess()
{
    m_debugger.NotifyProcessCreated();
    return false;
}

void CallbacksQueue::CallbacksWorker()
{
    std::unique_lock<std::mutex> lock(m_callbacksMutex);

    while (true)
    {
        while (m_callbacksQueue.empty() || m_stopEventInProcess)
        {
            // Note, during m_callbacksCV.wait() (waiting for notify_one call with entry added into queue),
            // m_callbacksMutex will be unlocked (see std::condition_variable for more info).
            m_callbacksCV.wait(lock);
        }

        auto &c = m_callbacksQueue.front();

        switch (c.Call)
        {
        case CallbackQueueCall::Breakpoint:
            m_stopEventInProcess = CallbacksWorkerBreakpoint(c.trAppDomain, c.trThread, c.trBreakpoint);
            break;
        case CallbackQueueCall::StepComplete:
            m_stopEventInProcess = CallbacksWorkerStepComplete(c.trThread, c.Reason);
            break;
        case CallbackQueueCall::Break:
            m_stopEventInProcess = CallbacksWorkerBreak(c.trAppDomain, c.trThread);
            break;
        case CallbackQueueCall::Exception:
            m_stopEventInProcess = CallbacksWorkerException(c.trAppDomain, c.trThread, c.EventType, c.ExcModule);
            break;
        case CallbackQueueCall::CreateProcess:
            m_stopEventInProcess = CallbacksWorkerCreateProcess();
            break;
        default:
            // finish loop
            // called from destructor only, don't need call pop()
            return;
        }

        ToRelease<ICorDebugAppDomain> trAppDomain(c.trAppDomain.Detach());
        m_callbacksQueue.pop_front();

        // Continue process execution only in case we don't have stop event emitted and queue is empty.
        // We safe here against fast Continue()/AddCallbackToQueue() call from new callback call, since we don't unlock m_callbacksMutex.
        // m_callbacksMutex will be unlocked only in m_callbacksCV.wait(), when CallbacksWorker will be ready for notify_one.
        if (m_callbacksQueue.empty() && !m_stopEventInProcess)
        {
            trAppDomain->Continue(0);
        }
    }
}

bool CallbacksQueue::HasQueuedCallbacks(ICorDebugProcess *pProcess)
{
    BOOL bQueued = FALSE;
    pProcess->HasQueuedCallbacks(nullptr, &bQueued);
    return bQueued == TRUE;
}

HRESULT CallbacksQueue::AddCallbackToQueue(ICorDebugAppDomain *pAppDomain, const std::function<void()> &callback)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        pAppDomain->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(m_callbacksMutex);

    callback();
    assert(!m_callbacksQueue.empty());

    // Note, we don't check m_callbacksQueue.empty() here, since callback() must add entry to queue.
    ToRelease<ICorDebugProcess> trProcess;
    if (SUCCEEDED(pAppDomain->GetProcess(&trProcess)) && HasQueuedCallbacks(trProcess))
    {
        pAppDomain->Continue(0);
    }
    else
    {
        m_callbacksCV.notify_one(); // notify_one with lock
    }

    return S_OK;
}

HRESULT CallbacksQueue::ContinueAppDomain(ICorDebugAppDomain *pAppDomain)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        if (pAppDomain == nullptr)
        {
            return E_NOTIMPL;
        }

        pAppDomain->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(m_callbacksMutex);

    ToRelease<ICorDebugProcess> trProcess;
    if (m_callbacksQueue.empty() ||
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
        m_callbacksCV.notify_one(); // notify_one with lock
    }

    return S_OK;
}

HRESULT CallbacksQueue::ContinueProcess(ICorDebugProcess *pProcess)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        if (pProcess == nullptr)
        {
            return E_NOTIMPL;
        }

        pProcess->Continue(0);
        return S_OK;
    }

    const std::unique_lock<std::mutex> lock(m_callbacksMutex);

    if (m_callbacksQueue.empty() || ((pProcess != nullptr) && HasQueuedCallbacks(pProcess)))
    {
        if (pProcess == nullptr)
        {
            return E_NOTIMPL;
        }

        pProcess->Continue(0);
    }
    else
    {
        m_callbacksCV.notify_one(); // notify_one with lock
    }

    return S_OK;
}

bool CallbacksQueue::IsRunning()
{
    const std::unique_lock<std::mutex> lock(m_callbacksMutex);
    return !m_stopEventInProcess;
}

HRESULT CallbacksQueue::Continue(ICorDebugProcess *pProcess)
{
    const std::unique_lock<std::mutex> lock(m_callbacksMutex);

    assert(m_stopEventInProcess);
    m_stopEventInProcess = false;

    if (m_callbacksQueue.empty())
    {
        return pProcess->Continue(0);
    }

    m_callbacksCV.notify_one(); // notify_one with lock
    return S_OK;
}

// Stop process and set last stopped thread.
HRESULT CallbacksQueue::Pause(ICorDebugProcess *pProcess, ThreadId lastStoppedThread)
{
    // Must be real thread ID or ThreadId::AllThreads.
    if (!lastStoppedThread)
    {
        return E_INVALIDARG;
    }

    const std::unique_lock<std::mutex> lock(m_callbacksMutex);

    if (m_stopEventInProcess)
    {
        return S_OK; // Already stopped.
    }

    HRESULT Status = S_OK;
    // Note, in case Stop() failed, no stop event will be emitted, don't set m_stopEventInProcess to "true" in this case.
    IfFailRet(pProcess->Stop(0));
    m_stopEventInProcess = true;

    // Same logic as provided by vsdbg in case of pause during stepping.
    m_debugger.m_uniqueSteppers->DisableAllSteppers(pProcess);

    std::vector<Thread> threads;
    m_debugger.GetThreads(threads);

    // In case of DAP, command provides "pause" thread id.
    if (std::find_if(threads.begin(), threads.end(),
                     [&](const Thread &t) { return t.id == lastStoppedThread; }) != threads.end())
    {
        // DAP event must provide thread only (VSCode IDE counts on this), even if this thread doesn't have user code.
        m_debugger.SetLastStoppedThreadId(lastStoppedThread);
        DAPIO::EmitStoppedEvent(StoppedEvent(StoppedEventReason::Pause, lastStoppedThread));
        return S_OK;
    }

    // Fatal error during stop (command provides wrong thread id), just fail Pause request and don't stop process.
    m_stopEventInProcess = false;
    IfFailRet(pProcess->Continue(0));
    return E_FAIL;
}

CallbacksQueue::~CallbacksQueue()
{
    std::unique_lock<std::mutex> lock(m_callbacksMutex);

    // Clear queue and do notify_one call with FinishWorker request.
    m_callbacksQueue.clear();
    m_callbacksQueue.emplace_front(CallbackQueueCall::FinishWorker, nullptr, nullptr, nullptr, STEP_NORMAL,
                                   ExceptionCallbackType::FIRST_CHANCE);
    m_stopEventInProcess = false; // forced to proceed during break too
    m_callbacksCV.notify_one();   // notify_one with lock
    lock.unlock();
    m_callbacksWorker.join();
}

// NOTE caller must care about m_callbacksMutex.
void CallbacksQueue::EmplaceBack(CallbackQueueCall Call, ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                 ICorDebugBreakpoint *pBreakpoint, CorDebugStepReason Reason,
                                 ExceptionCallbackType EventType, const std::string &ExcModule)
{
    m_callbacksQueue.emplace_back(Call, pAppDomain, pThread, pBreakpoint, Reason, EventType, ExcModule);
}

} // namespace dncdbg
