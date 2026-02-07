// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints.h"
#include "debugger/breakpoints/breakpoint_break.h"
#include "debugger/breakpoints/breakpoint_entry.h"
#include "debugger/breakpoints/breakpoints_exception.h"
#include "debugger/breakpoints/breakpoints_func.h"
#include "debugger/breakpoints/breakpoints_line.h"
#include "debugger/breakpoints/breakpointutils.h"
#include "metadata/modules.h"
#include <mutex>

namespace dncdbg
{

Breakpoints::Breakpoints(std::shared_ptr<Modules> &sharedModules, std::shared_ptr<Evaluator> &sharedEvaluator, std::shared_ptr<Variables> &sharedVariables)
        : m_breakBreakpoint(new BreakBreakpoint(sharedModules)),
          m_entryBreakpoint(new EntryBreakpoint(sharedModules)),
          m_exceptionBreakpoints(new ExceptionBreakpoints(sharedEvaluator)),
          m_funcBreakpoints(new FuncBreakpoints(sharedModules, sharedVariables)),
          m_lineBreakpoints(new LineBreakpoints(sharedModules, sharedVariables)),
          m_nextBreakpointId(1)
    {}

void Breakpoints::SetJustMyCode(bool enable)
{
    m_funcBreakpoints->SetJustMyCode(enable);
    m_lineBreakpoints->SetJustMyCode(enable);
    m_exceptionBreakpoints->SetJustMyCode(enable);
}

void Breakpoints::SetLastStoppedIlOffset(ICorDebugProcess *pProcess, const ThreadId &lastStoppedThreadId)
{
    m_breakBreakpoint->SetLastStoppedIlOffset(pProcess, lastStoppedThreadId);
}

void Breakpoints::SetStopAtEntry(bool enable)
{
    m_entryBreakpoint->SetStopAtEntry(enable);
}

HRESULT Breakpoints::ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId)
{
    return m_breakBreakpoint->ManagedCallbackBreak(pThread, lastStoppedThreadId);
}

void Breakpoints::DeleteAll()
{
    m_entryBreakpoint->Delete();
    m_funcBreakpoints->DeleteAll();
    m_lineBreakpoints->DeleteAll();
    m_exceptionBreakpoints->DeleteAll();
}

HRESULT Breakpoints::DisableAll(ICorDebugProcess *pProcess)
{
    HRESULT Status;
    ToRelease<ICorDebugAppDomainEnum> domains;
    IfFailRet(pProcess->EnumerateAppDomains(&domains));

    ICorDebugAppDomain *curDomain;
    ULONG domainsFetched;
    while (SUCCEEDED(domains->Next(1, &curDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> pDomain(curDomain);
        ToRelease<ICorDebugBreakpointEnum> breakpoints;
        if (FAILED(pDomain->EnumerateBreakpoints(&breakpoints)))
            continue;

        ICorDebugBreakpoint *curBreakpoint;
        ULONG breakpointsFetched;
        while (SUCCEEDED(breakpoints->Next(1, &curBreakpoint, &breakpointsFetched)) && breakpointsFetched == 1)
        {
            ToRelease<ICorDebugBreakpoint> pBreakpoint(curBreakpoint);
            pBreakpoint->Activate(FALSE);
        }
    }

    return S_OK;
}

HRESULT Breakpoints::SetFuncBreakpoints(bool haveProcess, const std::vector<FuncBreakpoint> &funcBreakpoints,
                                        std::vector<Breakpoint> &breakpoints)
{
    return m_funcBreakpoints->SetFuncBreakpoints(haveProcess, funcBreakpoints, breakpoints,
        [&]() -> uint32_t
        {
            const std::scoped_lock<std::mutex> lock(m_nextBreakpointIdMutex);
            return m_nextBreakpointId++;
        });
}

HRESULT Breakpoints::SetLineBreakpoints(bool haveProcess, const std::string &filename,
                                        const std::vector<LineBreakpoint> &lineBreakpoints,
                                        std::vector<Breakpoint> &breakpoints)
{
    return m_lineBreakpoints->SetLineBreakpoints(haveProcess, filename, lineBreakpoints, breakpoints,
        [&]() -> uint32_t
        {
            const std::scoped_lock<std::mutex> lock(m_nextBreakpointIdMutex);
            return m_nextBreakpointId++;
        });
}

HRESULT Breakpoints::SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints, std::vector<Breakpoint> &breakpoints)
{
    return m_exceptionBreakpoints->SetExceptionBreakpoints(exceptionBreakpoints, breakpoints,
        [&]() -> uint32_t
        {
            const std::scoped_lock<std::mutex> lock(m_nextBreakpointIdMutex);
            return m_nextBreakpointId++;
        });
}

HRESULT Breakpoints::GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo)
{
    return m_exceptionBreakpoints->GetExceptionInfo(pThread, exceptionInfo);
}

HRESULT Breakpoints::ManagedCallbackBreakpoint(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                               std::vector<BreakpointEvent> &bpChangeEvents, bool &atEntry)
{
    // CheckBreakpointHit return:
    //     S_OK - breakpoint hit
    //     S_FALSE - no breakpoint hit.
    // ManagedCallbackBreakpoint return:
    //     S_OK - callback should be interrupted without event emit
    //     S_FALSE - callback should not be interrupted and emit stop event

    HRESULT Status;
    atEntry = false;
    if (SUCCEEDED(Status = m_entryBreakpoint->CheckBreakpointHit(pBreakpoint)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        atEntry = true;
        return S_FALSE; // S_FALSE - not affect on callback (callback will emit stop event)
    }

    // Don't stop at breakpoint in not JMC code, if possible (error here is not fatal for debug process).
    // We need this check here, since we can't guarantee this check in SkipBreakpoint().
    ToRelease<ICorDebugFrame> iCorFrame;
    ToRelease<ICorDebugFunction> iCorFunction;
    ToRelease<ICorDebugFunction2> iCorFunction2;
    BOOL JMCStatus;
    if (SUCCEEDED(pThread->GetActiveFrame(&iCorFrame)) && iCorFrame != nullptr &&
        SUCCEEDED(iCorFrame->GetFunction(&iCorFunction)) &&
        SUCCEEDED(iCorFunction->QueryInterface(IID_ICorDebugFunction2, (LPVOID *)&iCorFunction2)) &&
        SUCCEEDED(iCorFunction2->GetJMCStatus(&JMCStatus)) &&
        JMCStatus == FALSE)
    {
        return S_OK; // forced to interrupt this callback (breakpoint in not user code, continue process execution)
    }

    if (SUCCEEDED(Status = m_lineBreakpoints->CheckBreakpointHit(pThread, pBreakpoint, bpChangeEvents)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_FALSE; // S_FALSE - not affect on callback (callback will emit stop event)
    }

    if (SUCCEEDED(Status = m_funcBreakpoints->CheckBreakpointHit(pThread, pBreakpoint, bpChangeEvents)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_FALSE; // S_FALSE - not affect on callback (callback will emit stop event)
    }

    return S_OK; // no breakpoints hit, forced to interrupt this callback
}

HRESULT Breakpoints::ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    m_entryBreakpoint->ManagedCallbackLoadModule(pModule);
    m_funcBreakpoints->ManagedCallbackLoadModule(pModule, events);
    m_lineBreakpoints->ManagedCallbackLoadModule(pModule, events);
    return S_OK;
}

HRESULT Breakpoints::ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType,
                                              const std::string &excModule)
{
    return m_exceptionBreakpoints->ManagedCallbackException(pThread, eventType, excModule);
}

HRESULT Breakpoints::ManagedCallbackExitThread(ICorDebugThread *pThread)
{
    return m_exceptionBreakpoints->ManagedCallbackExitThread(pThread);
}

} // namespace dncdbg
