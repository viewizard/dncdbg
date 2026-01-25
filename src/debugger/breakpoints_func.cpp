// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints_func.h"
#include "debugger/breakpointutils.h"
#include "metadata/typeprinter.h"
#include "metadata/modules.h"
#include <sstream>
#include <unordered_set>
#include <algorithm>

namespace dncdbg
{

void FuncBreakpoints::ManagedFuncBreakpoint::ToBreakpoint(Breakpoint &breakpoint) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
    breakpoint.condition = this->condition;
    breakpoint.module = this->module;
    breakpoint.funcname = this->name;
    breakpoint.params = this->params;
}

void FuncBreakpoints::DeleteAll()
{
    m_breakpointsMutex.lock();
    m_funcBreakpoints.clear();
    m_breakpointsMutex.unlock();
}

HRESULT FuncBreakpoints::CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint, Breakpoint &breakpoint, std::vector<BreakpointEvent> &bpChangeEvents)
{
    if (m_funcBreakpoints.empty())
        return S_FALSE; // Stopped at break, but no breakpoints.

    HRESULT Status;
    ToRelease<ICorDebugFunctionBreakpoint> pFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, (LPVOID *) &pFunctionBreakpoint));

    ToRelease<ICorDebugFrame> pFrame;
    IfFailRet(pThread->GetActiveFrame(&pFrame));
    if (pFrame == nullptr)
        return E_FAIL;

    ToRelease<ICorDebugILFrame> pILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, (LPVOID *) &pILFrame));

    ToRelease<ICorDebugValueEnum> pParamEnum;
    IfFailRet(pILFrame->EnumerateArguments(&pParamEnum));
    ULONG cParams = 0;
    IfFailRet(pParamEnum->GetCount(&cParams));

    std::ostringstream ss;
    ss << "(";
    if (cParams > 0)
    {
        for (ULONG i = 0; i < cParams; ++i)
        {
            ToRelease<ICorDebugValue> pValue;
            ULONG cArgsFetched;
            if (FAILED(pParamEnum->Next(1, &pValue, &cArgsFetched)))
                continue;

            std::string param;
            IfFailRet(TypePrinter::GetTypeOfValue(pValue, param));
            if (i > 0)
                ss << ",";

            ss << param;
        }
    }
    ss << ")";
    std::string params = ss.str();

    // Note, since IsEnableByCondition() during eval execution could neutered frame, all frame-related calculation
    // must be done before enter into this cycles.
    for (auto &fb : m_funcBreakpoints)
    {
        ManagedFuncBreakpoint &fbp = fb.second;

        if (!fbp.params.empty() && params != fbp.params)
            continue;

        for (auto &iCorFuncBreakpoint : fbp.iCorFuncBreakpoints)
        {
            IfFailRet(BreakpointUtils::IsSameFunctionBreakpoint(pFunctionBreakpoint, iCorFuncBreakpoint));
            if (Status == S_FALSE)
                continue;

            std::string output;
            if (FAILED(Status = BreakpointUtils::IsEnableByCondition(fbp.condition, m_sharedVariables.get(), pThread, output)))
            {
                if (output.empty())
                    return Status;
            }
            if (Status == S_FALSE)
                continue;

            ++fbp.times;
            fbp.ToBreakpoint(breakpoint);

            if (!output.empty())
            {
                breakpoint.message = "The condition for a breakpoint failed to execute. The condition was '" + fbp.condition + "'. The error returned was '" + output + "'.";
                bpChangeEvents.emplace_back(BreakpointChanged, breakpoint);
            }

            return S_OK;
        }
    }

    return S_FALSE; // Stopped at break, but breakpoint not found.
}

HRESULT FuncBreakpoints::ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    std::lock_guard<std::mutex> lock(m_breakpointsMutex);

    for (auto &funcBreakpoints : m_funcBreakpoints)
    {
        ManagedFuncBreakpoint &fb = funcBreakpoints.second;

        if (fb.IsResolved() ||
            FAILED(ResolveFuncBreakpointInModule(pModule, fb)))
            continue;

        Breakpoint breakpoint;
        fb.ToBreakpoint(breakpoint);
        events.emplace_back(BreakpointChanged, breakpoint);
    }

    return S_OK;
}

HRESULT FuncBreakpoints::SetFuncBreakpoints(bool haveProcess, const std::vector<FuncBreakpoint> &funcBreakpoints,
                                            std::vector<Breakpoint> &breakpoints, std::function<uint32_t()> getId)
{
    std::lock_guard<std::mutex> lock(m_breakpointsMutex);

    // Remove old breakpoints
    std::unordered_set<std::string> funcBreakpointFuncs;
    for (const auto &fb : funcBreakpoints)
    {
        std::string fullFuncName("");
        if (!fb.module.empty())
        {
            fullFuncName = fb.module + "!";
        }
        fullFuncName += fb.func + fb.params;
        funcBreakpointFuncs.insert(fullFuncName);
    }
    for (auto it = m_funcBreakpoints.begin(); it != m_funcBreakpoints.end();)
    {
        if (funcBreakpointFuncs.find(it->first) == funcBreakpointFuncs.end())
            it = m_funcBreakpoints.erase(it);
        else
            ++it;
    }

    if (funcBreakpoints.empty())
        return S_OK;


    // Export function breakpoints
    // Note, DAP protocol require, that "breakpoints" and "funcBreakpoints" must have same indexes for same breakpoints.

    for (const auto &fb : funcBreakpoints)
    {
        std::string fullFuncName("");

        if (!fb.module.empty())
            fullFuncName = fb.module + "!";

        fullFuncName += fb.func + fb.params;

        Breakpoint breakpoint;

        auto b = m_funcBreakpoints.find(fullFuncName);
        if (b == m_funcBreakpoints.end())
        {
            // New function breakpoint
            ManagedFuncBreakpoint fbp;
            fbp.id = getId();
            fbp.module = fb.module;
            fbp.name = fb.func;
            fbp.params = fb.params;
            fbp.condition = fb.condition;

            if (haveProcess)
                ResolveFuncBreakpoint(fbp);

            fbp.ToBreakpoint(breakpoint);
            m_funcBreakpoints.insert(std::make_pair(fullFuncName, std::move(fbp)));
        }
        else
        {
            ManagedFuncBreakpoint &fbp = b->second;

            fbp.condition = fb.condition;
            fbp.ToBreakpoint(breakpoint);
        }

        breakpoints.push_back(breakpoint);
    }

    return S_OK;
}

HRESULT FuncBreakpoints::AddFuncBreakpoint(ManagedFuncBreakpoint &fbp, ResolvedFBP &fbpResolved)
{
    HRESULT Status;

    for (auto &entry : fbpResolved)
    {
        IfFailRet(BreakpointUtils::SkipBreakpoint(entry.first, entry.second, m_justMyCode));
        if (Status == S_OK) // S_FALSE - don't skip breakpoint
            return S_OK;

        ToRelease<ICorDebugFunction> pFunc;
        IfFailRet(entry.first->GetFunctionFromToken(entry.second, &pFunc));

        ULONG32 ilNextOffset = 0;
        if (FAILED(m_sharedModules->GetNextUserCodeILOffsetInMethod(entry.first, entry.second, 0, ilNextOffset)))
            return S_OK;

        ToRelease<ICorDebugCode> pCode;
        IfFailRet(pFunc->GetILCode(&pCode));

        ToRelease<ICorDebugFunctionBreakpoint> iCorFuncBreakpoint;
        IfFailRet(pCode->CreateBreakpoint(ilNextOffset, &iCorFuncBreakpoint));
        IfFailRet(iCorFuncBreakpoint->Activate(TRUE));

        fbp.iCorFuncBreakpoints.emplace_back(iCorFuncBreakpoint.Detach());
    }

    return S_OK;
}

HRESULT FuncBreakpoints::ResolveFuncBreakpoint(ManagedFuncBreakpoint &fbp)
{
    HRESULT Status;
    ResolvedFBP fbpResolved;

    IfFailRet(m_sharedModules->ResolveFuncBreakpointInAny(
        fbp.module, fbp.module_checked, fbp.name, 
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
    {
        fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
        return S_OK;
    }));

    return AddFuncBreakpoint(fbp, fbpResolved);
}

HRESULT FuncBreakpoints::ResolveFuncBreakpointInModule(ICorDebugModule *pModule, ManagedFuncBreakpoint &fbp)
{
    HRESULT Status;
    ResolvedFBP fbpResolved;

    IfFailRet(m_sharedModules->ResolveFuncBreakpointInModule(
        pModule, fbp.module, fbp.module_checked, fbp.name,
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
    {
        fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
        return S_OK;
    }));

    return AddFuncBreakpoint(fbp, fbpResolved);
}


} // namespace dncdbg
