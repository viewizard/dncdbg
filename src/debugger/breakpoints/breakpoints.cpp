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
#include "debugger/breakpoints/breakpointutils.h"
#include "debuginfo/debuginfo.h"
#include <mutex>

namespace dncdbg
{

Breakpoints::Breakpoints(std::shared_ptr<DebugInfo> &sharedDebugInfo,
                         std::shared_ptr<Evaluator> &sharedEvaluator,
                         std::shared_ptr<Variables> &sharedVariables)
    : m_breakBreakpoint(new BreakBreakpoint(sharedDebugInfo)),
      m_entryBreakpoint(new EntryBreakpoint(sharedDebugInfo)),
      m_exceptionBreakpoints(new ExceptionBreakpoints(sharedEvaluator)),
      m_funcBreakpoints(new FunctionBreakpoints(sharedDebugInfo, sharedVariables)),
      m_sourceBreakpoints(new SourceBreakpoints(sharedDebugInfo, sharedVariables))
{
}

void Breakpoints::SetJustMyCode(bool enable)
{
    m_funcBreakpoints->SetJustMyCode(enable);
    m_sourceBreakpoints->SetJustMyCode(enable);
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
    // Must be called on DetachFromProcess() and TerminateProcess() only. We assume that
    // the current debugging process will not continue after this call.
    // Clear all breakpoint structures, no need to waste time deactivating breakpoints.

    m_entryBreakpoint->Delete();
    m_funcBreakpoints->DeleteAll();
    m_sourceBreakpoints->DeleteAll();
    m_exceptionBreakpoints->DeleteAll();

    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());
    GetManagedBreakpoints().clear();
}

HRESULT Breakpoints::DisableAll(ICorDebugProcess *pProcess)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    IfFailRet(pProcess->EnumerateAppDomains(&trAppDomainEnum));

    ICorDebugAppDomain *curDomain = nullptr;
    ULONG domainsFetched = 0;
    while (SUCCEEDED(trAppDomainEnum->Next(1, &curDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> trDomain(curDomain);
        ToRelease<ICorDebugBreakpointEnum> trBreakpointEnum;
        if (FAILED(trDomain->EnumerateBreakpoints(&trBreakpointEnum)))
        {
            continue;
        }

        ICorDebugBreakpoint *curBreakpoint = nullptr;
        ULONG breakpointsFetched = 0;
        while (SUCCEEDED(trBreakpointEnum->Next(1, &curBreakpoint, &breakpointsFetched)) && breakpointsFetched == 1)
        {
            ToRelease<ICorDebugBreakpoint> trBreakpoint(curBreakpoint);
            trBreakpoint->Activate(FALSE);
        }
    }

    return S_OK;
}

HRESULT Breakpoints::SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                            std::vector<Breakpoint> &breakpoints)
{
    return m_funcBreakpoints->SetFunctionBreakpoints(haveProcess, functionBreakpoints, breakpoints,
        [&]() -> uint32_t
        {
            const std::scoped_lock<std::mutex> lock(m_nextBreakpointIdMutex);
            return m_nextBreakpointId++;
        });
}

HRESULT Breakpoints::SetSourceBreakpoints(bool haveProcess, const std::string &filename,
                                          const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                          std::vector<Breakpoint> &breakpoints)
{
    return m_sourceBreakpoints->SetSourceBreakpoints(haveProcess, filename, sourceBreakpoints, breakpoints,
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
                                               std::vector<uint32_t> &hitBreakpointIds,
                                               std::vector<BreakpointEvent> &bpChangeEvents, bool &atEntry)
{
    // CheckBreakpointHit return:
    //     S_OK - breakpoint hit
    //     S_FALSE - no breakpoint hit.
    // ManagedCallbackBreakpoint return:
    //     S_OK - callback execution should not be interrupted with stop event emit
    //     S_IGNORE - callback execution should be interrupted without event emit

    HRESULT Status = S_OK;
    atEntry = false;
    if (SUCCEEDED(Status = m_entryBreakpoint->CheckBreakpointHit(pBreakpoint)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        atEntry = true;
        return S_OK;
    }

    // Don't stop at breakpoint in not JMC code, if possible (error here is not fatal for debug process).
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

    if (SUCCEEDED(Status = m_sourceBreakpoints->CheckBreakpointHit(pThread, pBreakpoint, hitBreakpointIds, bpChangeEvents)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_OK;
    }

    if (SUCCEEDED(Status = m_funcBreakpoints->CheckBreakpointHit(pThread, pBreakpoint, hitBreakpointIds, bpChangeEvents)) &&
        Status == S_OK) // S_FALSE - no breakpoint hit
    {
        return S_OK;
    }

    return S_IGNORE;
}

HRESULT Breakpoints::ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    m_entryBreakpoint->ManagedCallbackLoadModule(pModule);
    m_funcBreakpoints->ManagedCallbackLoadModule(pModule, events);
    m_sourceBreakpoints->ManagedCallbackLoadModule(pModule, events);
    return S_OK;
}

HRESULT Breakpoints::ManagedCallbackUnloadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    m_funcBreakpoints->ManagedCallbackUnloadModule(pModule, events);
    m_sourceBreakpoints->ManagedCallbackUnloadModule(pModule, events);
    return S_OK;
}

HRESULT Breakpoints::ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType)
{
    return m_exceptionBreakpoints->ManagedCallbackException(pThread, eventType);
}

HRESULT Breakpoints::ManagedCallbackExitThread(ICorDebugThread *pThread)
{
    return m_exceptionBreakpoints->ManagedCallbackExitThread(pThread);
}

#ifdef DEBUG_INTERNAL_TESTS
size_t Breakpoints::GetBreakpointsCount()
{
    return m_funcBreakpoints->GetBreakpointsCount() +
           m_sourceBreakpoints->GetBreakpointsCount();
}
#endif // DEBUG_INTERNAL_TESTS

HRESULT Breakpoints::ActivateManagedBreakpoint(CORDB_ADDRESS modAddress, uint32_t methodToken, uint32_t ilOffset,
                                               ICorDebugModule *pModule, ICorDebugFunctionBreakpoint **ppFuncBreakpoint)
{
    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());

    auto find = GetManagedBreakpoints().find({modAddress, methodToken, ilOffset});
    if (find != GetManagedBreakpoints().end())
    {
        find->second.trBreakpoint->AddRef();
        find->second.refCount++;
        *ppFuncBreakpoint = find->second.trBreakpoint;
        return S_OK;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunc));
    ToRelease<ICorDebugCode> trCode;
    IfFailRet(trFunc->GetILCode(&trCode));
    IfFailRet(trCode->CreateBreakpoint(ilOffset, ppFuncBreakpoint));
    IfFailRet((*ppFuncBreakpoint)->Activate(TRUE));

    (*ppFuncBreakpoint)->AddRef();
    GetManagedBreakpoints().emplace(BreakpointLocation(modAddress, methodToken, ilOffset), BreakpointData(*ppFuncBreakpoint, 2));

    return S_OK;
}

HRESULT Breakpoints::DeactivateManagedBreakpoint(ToRelease<ICorDebugFunctionBreakpoint> &trFuncBreakpoint)
{
    if (trFuncBreakpoint == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;

    uint32_t ilOffset = 0;
    IfFailRet(trFuncBreakpoint->GetOffset(&ilOffset));
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFuncBreakpoint->GetFunction(&trFunction));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());

    auto find = GetManagedBreakpoints().find({modAddress, methodToken, ilOffset});
    if (find == GetManagedBreakpoints().end())
    {
        return E_FAIL;
    }

    trFuncBreakpoint.Free();
    find->second.refCount--;

    assert(find->second.refCount >= 1);

    if (find->second.refCount == 1)
    {
        find->second.trBreakpoint->Activate(FALSE);
        GetManagedBreakpoints().erase(find);
    }
    return S_OK;
}

} // namespace dncdbg
