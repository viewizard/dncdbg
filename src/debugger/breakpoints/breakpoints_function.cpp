// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints_function.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/breakpoints/breakpointutils.h"
#include "debuginfo/debuginfo.h" // NOLINT(misc-include-cleaner)
#include "metadata/typeprinter.h"
#include <sstream>
#include <unordered_set>

namespace dncdbg
{

void FunctionBreakpoints::ManagedFunctionBreakpoint::ToBreakpoint(Breakpoint &breakpoint) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
}

void FunctionBreakpoints::DeleteAll()
{
    m_breakpointsMutex.lock();
    m_funcBreakpoints.clear();
    m_breakpointsMutex.unlock();
}

HRESULT FunctionBreakpoints::CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                                std::vector<BreakpointEvent> &bpChangeEvents)
{
    if (m_funcBreakpoints.empty())
    {
        return S_FALSE; // Stopped at break, but no breakpoints.
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> trFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&trFunctionBreakpoint)));

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    ToRelease<ICorDebugValueEnum> trParamEnum;
    IfFailRet(trILFrame->EnumerateArguments(&trParamEnum));
    ULONG cParams = 0;
    IfFailRet(trParamEnum->GetCount(&cParams));

    std::ostringstream ss;
    ss << "(";
    if (cParams > 0)
    {
        for (ULONG i = 0; i < cParams; ++i)
        {
            ToRelease<ICorDebugValue> trValue;
            ULONG cArgsFetched = 0;
            if (FAILED(trParamEnum->Next(1, &trValue, &cArgsFetched)))
            {
                continue;
            }

            std::string param;
            IfFailRet(TypePrinter::GetTypeOfValue(trValue, param));
            if (i > 0)
            {
                ss << ",";
            }

            ss << param;
        }
    }
    ss << ")";
    const std::string params = ss.str();

    // Note, since IsEnableByCondition() during eval execution could neutered frame, all frame-related calculation
    // must be done before enter into this cycles.
    for (auto &fb : m_funcBreakpoints)
    {
        ManagedFunctionBreakpoint &fbp = fb.second;

        if (!fbp.params.empty() && params != fbp.params)
        {
            continue;
        }

        for (auto &trFuncBreakpoint : fbp.trFuncBreakpoints)
        {
            IfFailRet(BreakpointUtils::IsSameFunctionBreakpoint(trFunctionBreakpoint, trFuncBreakpoint));
            if (Status == S_FALSE)
            {
                continue;
            }

            std::string output;
            if (FAILED(Status = BreakpointUtils::IsEnableByCondition(fbp.condition, m_sharedVariables.get(), pThread, output)))
            {
                if (output.empty())
                {
                    return Status;
                }
            }
            if (Status == S_FALSE)
            {
                continue;
            }

            ++fbp.hitCount;

            if (!output.empty())
            {
                Breakpoint breakpoint;
                fbp.ToBreakpoint(breakpoint);
                std::ostringstream ss;
                ss << "Breakpoint error: The condition for a breakpoint failed to execute. The condition was '"
                   << fbp.condition << "'. The error returned was '" << output << "'. - "
                   << fbp.name << "(" << fbp.params << ")\n";
                breakpoint.message = ss.str();
                bpChangeEvents.emplace_back(BreakpointEventReason::Changed, breakpoint);
            }

            return S_OK;
        }
    }

    return S_FALSE; // Stopped at break, but breakpoint not found.
}

HRESULT FunctionBreakpoints::ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    for (auto &functionBreakpoints : m_funcBreakpoints)
    {
        ManagedFunctionBreakpoint &fb = functionBreakpoints.second;

        if (FAILED(ResolveFunctionBreakpointInModule(pModule, fb)))
        {
            continue;
        }

        Breakpoint breakpoint;
        fb.ToBreakpoint(breakpoint);
        events.emplace_back(BreakpointEventReason::Changed, breakpoint);
    }

    return S_OK;
}

HRESULT FunctionBreakpoints::SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                                    std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId)
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    // Remove old breakpoints
    std::unordered_set<std::string> funcBreakpointFuncs;
    for (const auto &fb : functionBreakpoints)
    {
        const std::string fullFuncName = fb.func + fb.params;
        funcBreakpointFuncs.insert(fullFuncName);
    }
    for (auto it = m_funcBreakpoints.begin(); it != m_funcBreakpoints.end();)
    {
        if (funcBreakpointFuncs.find(it->first) == funcBreakpointFuncs.end())
        {
            it = m_funcBreakpoints.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (functionBreakpoints.empty())
    {
        return S_OK;
    }

    // Note, DAP require, that "sourceBreakpoints" and "functionBreakpoints" must have same indexes for same breakpoints.

    for (const auto &fb : functionBreakpoints)
    {
        const std::string fullFuncName = fb.func + fb.params;
        Breakpoint breakpoint;

        auto b = m_funcBreakpoints.find(fullFuncName);
        if (b == m_funcBreakpoints.end())
        {
            // New function breakpoint
            ManagedFunctionBreakpoint fbp;
            fbp.id = getId();
            fbp.name = fb.func;
            fbp.params = fb.params;
            fbp.condition = fb.condition;

            if (haveProcess)
            {
                ResolveFunctionBreakpoint(fbp);
            }

            fbp.ToBreakpoint(breakpoint);
            m_funcBreakpoints.insert(std::make_pair(fullFuncName, std::move(fbp)));
        }
        else
        {
            ManagedFunctionBreakpoint &fbp = b->second;

            fbp.condition = fb.condition;
            fbp.ToBreakpoint(breakpoint);
        }

        breakpoints.push_back(breakpoint);
    }

    return S_OK;
}

HRESULT FunctionBreakpoints::AddFunctionBreakpoint(ManagedFunctionBreakpoint &fbp, ResolvedFBP &fbpResolved)
{
    HRESULT Status = S_OK;

    for (auto &entry : fbpResolved)
    {
        IfFailRet(BreakpointUtils::SkipBreakpoint(entry.first, entry.second, m_justMyCode));
        if (Status == S_OK) // S_FALSE - don't skip breakpoint
        {
            return S_OK;
        }

        ToRelease<ICorDebugFunction> trFunc;
        IfFailRet(entry.first->GetFunctionFromToken(entry.second, &trFunc));

        uint32_t ilNextOffset = 0;
        if (FAILED(m_sharedDebugInfo->GetNextUserCodeILOffsetInMethod(entry.first, entry.second, 0, ilNextOffset)))
        {
            return S_OK;
        }

        ToRelease<ICorDebugCode> trCode;
        IfFailRet(trFunc->GetILCode(&trCode));

        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(trCode->CreateBreakpoint(ilNextOffset, &trFuncBreakpoint));
        IfFailRet(trFuncBreakpoint->Activate(TRUE));

        fbp.trFuncBreakpoints.emplace_back(trFuncBreakpoint.Detach());
    }

    return S_OK;
}

HRESULT FunctionBreakpoints::ResolveFunctionBreakpoint(ManagedFunctionBreakpoint &fbp)
{
    HRESULT Status = S_OK;
    ResolvedFBP fbpResolved;

    IfFailRet(m_sharedDebugInfo->ResolveFunctionBreakpointInAny(fbp.name,
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
        {
            fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
            return S_OK;
        }));

    return AddFunctionBreakpoint(fbp, fbpResolved);
}

HRESULT FunctionBreakpoints::ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, ManagedFunctionBreakpoint &fbp)
{
    HRESULT Status = S_OK;
    ResolvedFBP fbpResolved;

    IfFailRet(m_sharedDebugInfo->ResolveFunctionBreakpointInModule(
        pModule, fbp.name,
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
        {
            fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
            return S_OK;
        }));

    return AddFunctionBreakpoint(fbp, fbpResolved);
}

} // namespace dncdbg
