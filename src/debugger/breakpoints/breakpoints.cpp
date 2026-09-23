// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints.h"
#include "debugger/breakpoints/breakpoint_break.h"
#include "debugger/breakpoints/breakpoint_entry.h"
#include "debugger/breakpoints/breakpoints_exception.h"
#include "debugger/breakpoints/breakpoints_function.h"
#include "debugger/breakpoints/breakpoints_source.h"
#include "debugger/breakpoints/internal_helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <mutex>

namespace dncdbg::Breakpoints
{

namespace
{

std::mutex &GetNextBreakpointIdMutex()
{
    static std::mutex nextBreakpointIdMutex;
    return nextBreakpointIdMutex;
}

uint32_t &GetNextBreakpointId()
{
    static uint32_t nextBreakpointId{1};
    return nextBreakpointId;
}

uint32_t GetNewBreakpointId()
{
    const std::scoped_lock<std::mutex> lock(GetNextBreakpointIdMutex());
    return GetNextBreakpointId()++;
}

} // unnamed namespace

void SetLastStoppedIlOffset(ICorDebugThread *pThread)
{
    BreakBreakpoint::SetLastStoppedIlOffset(pThread);
}

HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId)
{
    return BreakBreakpoint::ManagedCallbackBreak(pThread, lastStoppedThreadId);
}

HRESULT DisableAll(ICorDebugProcess *pProcess)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    IfFailRet(pProcess->EnumerateAppDomains(&trAppDomainEnum));

    ICorDebugAppDomain *pCurDomain = nullptr;
    ULONG domainsFetched = 0;
    while (SUCCEEDED(trAppDomainEnum->Next(1, &pCurDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> trDomain(pCurDomain);
        ToRelease<ICorDebugBreakpointEnum> trBreakpointEnum;
        if (FAILED(trDomain->EnumerateBreakpoints(&trBreakpointEnum)))
        {
            continue;
        }

        ICorDebugBreakpoint *pCurBreakpoint = nullptr;
        ULONG breakpointsFetched = 0;
        while (SUCCEEDED(trBreakpointEnum->Next(1, &pCurBreakpoint, &breakpointsFetched)) && breakpointsFetched == 1)
        {
            ToRelease<ICorDebugBreakpoint> trBreakpoint(pCurBreakpoint);
            trBreakpoint->Activate(FALSE);
        }
    }

    return S_OK;
}

HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints)
{
    return FunctionBreakpoints::SetFunctionBreakpoints(haveProcess, functionBreakpoints, breakpoints, GetNewBreakpointId);
}

HRESULT SetSourceBreakpoints(bool haveProcess, const Source &source,
                             const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints)
{
    return SourceBreakpoints::SetSourceBreakpoints(haveProcess, source, sourceBreakpoints, breakpoints, GetNewBreakpointId);
}

HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints, std::vector<Breakpoint> &breakpoints)
{
    return ExceptionBreakpoints::SetExceptionBreakpoints(exceptionBreakpoints, breakpoints, GetNewBreakpointId);
}

HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo)
{
    return ExceptionBreakpoints::GetExceptionInfo(pThread, exceptionInfo);
}

HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                  std::vector<uint32_t> &hitBreakpointIds, bool &atEntry)
{
    // CheckBreakpointHit return:
    //     S_OK - breakpoint hit
    //     S_FALSE - no breakpoint hit.
    // ManagedCallbackBreakpoint return:
    //     S_OK - callback execution should not be interrupted with stop event emit
    //     S_IGNORE - callback execution should be interrupted without event emit

    HRESULT Status = S_OK;
    atEntry = false;
    if (SUCCEEDED(Status = EntryBreakpoint::CheckBreakpointHit(pBreakpoint)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        atEntry = true;
        return S_OK;
    }

    // Don't stop at breakpoint in non-JMC code, if possible (error here is not fatal for debug process).
    // We need this check here, since we can't guarantee this check in SkipBreakpoint().
    ToRelease<ICorDebugFrame> trFrame;
    ToRelease<ICorDebugFunction> trFunction;
    ToRelease<ICorDebugFunction2> trFunction2;
    BOOL JMCStatus = FALSE;
    if (SUCCEEDED(pThread->GetActiveFrame(&trFrame)) && trFrame != nullptr &&
        SUCCEEDED(trFrame->GetFunction(&trFunction)) &&
        SUCCEEDED(trFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&trFunction2))) &&
        SUCCEEDED(trFunction2->GetJMCStatus(&JMCStatus)) &&
        JMCStatus == FALSE)
    {
        return S_IGNORE; // breakpoint in non-user code, continue process execution
    }

    if (SUCCEEDED(Status = SourceBreakpoints::CheckBreakpointHit(pThread, pBreakpoint, hitBreakpointIds)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_OK;
    }

    if (SUCCEEDED(Status = FunctionBreakpoints::CheckBreakpointHit(pThread, pBreakpoint, hitBreakpointIds)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_OK;
    }

    return S_IGNORE;
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    EntryBreakpoint::ManagedCallbackLoadModule(pModule);
    FunctionBreakpoints::ManagedCallbackLoadModule(pModule);
    SourceBreakpoints::ManagedCallbackLoadModule(pModule);
    return S_OK;
}

HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    FunctionBreakpoints::ManagedCallbackUnloadModule(pModule);
    SourceBreakpoints::ManagedCallbackUnloadModule(pModule);
    return S_OK;
}

HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType)
{
    return ExceptionBreakpoints::ManagedCallbackException(pThread, eventType);
}

HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread)
{
    return ExceptionBreakpoints::ManagedCallbackExitThread(pThread);
}

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount()
{
    return FunctionBreakpoints::GetBreakpointsCount() +
           SourceBreakpoints::GetBreakpointsCount();
}
#endif // DEBUG_INTERNAL_TESTS

void Cleanup()
{
    BreakBreakpoint::Cleanup();
    EntryBreakpoint::Cleanup();
    FunctionBreakpoints::Cleanup();
    SourceBreakpoints::Cleanup();
    ExceptionBreakpoints::Cleanup();
    BreakpointHelpers::Cleanup();

    const std::scoped_lock<std::mutex> lock(GetNextBreakpointIdMutex());
    GetNextBreakpointId() = 1;
}

} // namespace dncdbg::Breakpoints
