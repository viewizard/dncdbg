// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints_function.h"
#include "debugger/breakpoints/helpers.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "protocol/dapio.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <functional>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dncdbg::FunctionBreakpoints
{

namespace
{

bool &GetJustMyCode()
{
    static bool justMyCode{true};
    return justMyCode;
}

struct ManagedFunctionBreakpoint
{
    uint32_t id{0};
    std::string name;
    std::string params;
    uint32_t hitCount{0};
    std::string hitCondition;
    std::string condition;
    std::list<std::pair<ToRelease<ICorDebugFunctionBreakpoint>, CORDB_ADDRESS>> trFuncBreakpoints;

    [[nodiscard]] bool IsVerified() const
    {
        return !trFuncBreakpoints.empty();
    }

    void Reset()
    {
        hitCount = 0;
        trFuncBreakpoints.clear();
    }

    ManagedFunctionBreakpoint() = default;
    ~ManagedFunctionBreakpoint();

    void ToBreakpoint(Breakpoint &breakpoint) const;

    ManagedFunctionBreakpoint(ManagedFunctionBreakpoint &&) = default;
    ManagedFunctionBreakpoint(const ManagedFunctionBreakpoint &) = delete;
    ManagedFunctionBreakpoint &operator=(ManagedFunctionBreakpoint &&) = default;
    ManagedFunctionBreakpoint &operator=(const ManagedFunctionBreakpoint &) = delete;
};

ManagedFunctionBreakpoint::~ManagedFunctionBreakpoint()
{
    for (auto &[trFuncBreakpoint, nativeAddress] : trFuncBreakpoints)
    {
        BreakpointHelpers::DeactivateManagedBreakpoint(trFuncBreakpoint);
    }
}

void ManagedFunctionBreakpoint::ToBreakpoint(Breakpoint &breakpoint) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
}

std::mutex &GetBreakpointsMutex()
{
    static std::mutex breakpointsMutex;
    return breakpointsMutex;
}

std::unordered_map<std::string, ManagedFunctionBreakpoint> &GetFuncBreakpoints()
{
    static std::unordered_map<std::string, ManagedFunctionBreakpoint> funcBreakpoints;
    return funcBreakpoints;
}

using ResolvedFBP = std::vector<std::pair<ICorDebugModule *, mdMethodDef>>;
HRESULT AddFunctionBreakpoint(const ResolvedFBP &fbpResolved, ManagedFunctionBreakpoint &fbp)
{
    HRESULT Status = S_OK;

    for (const auto &entry : fbpResolved)
    {
        const mdMethodDef &methodToken = entry.second;
        ICorDebugModule *pModule = entry.first;

        IfFailRet(BreakpointHelpers::SkipBreakpoint(pModule, methodToken, GetJustMyCode()));
        if (Status == S_SKIP)
        {
            return S_OK;
        }

        ToRelease<ICorDebugFunction> trFunc;
        IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunc));

        uint32_t ilOffset = 0;
        if (FAILED(DebugInfo::GetNextUserCodeILOffset(pModule, methodToken, 0, ilOffset)))
        {
            return S_OK;
        }

        CORDB_ADDRESS modAddress = 0;
        IfFailRet(pModule->GetBaseAddress(&modAddress));
        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(BreakpointHelpers::ActivateManagedBreakpoint(modAddress, methodToken, ilOffset, pModule, &trFuncBreakpoint));
        CORDB_ADDRESS nativeAddress = 0;
        BreakpointHelpers::GetBreakpointNativeAddress(trFuncBreakpoint, nativeAddress);
        fbp.trFuncBreakpoints.emplace_back(trFuncBreakpoint.Detach(), nativeAddress);
    }

    return S_OK;
}

HRESULT ResolveFunctionBreakpoint(ManagedFunctionBreakpoint &fbp)
{
    HRESULT Status = S_OK;
    ResolvedFBP fbpResolved;

    IfFailRet(DebugInfo::ResolveFunctionBreakpointInAny(fbp.name,
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
        {
            fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
            return S_OK;
        }));

    return AddFunctionBreakpoint(fbpResolved, fbp);
}

HRESULT ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, ManagedFunctionBreakpoint &fbp)
{
    HRESULT Status = S_OK;
    ResolvedFBP fbpResolved;

    IfFailRet(DebugInfo::ResolveFunctionBreakpointInModule(
        pModule, fbp.name,
        [&](ICorDebugModule *pModule, mdMethodDef &methodToken) -> HRESULT
        {
            fbpResolved.emplace_back(std::make_pair(pModule, methodToken));
            return S_OK;
        }));

    return AddFunctionBreakpoint(fbpResolved, fbp);
}

} // unnamed namespace

void SetJustMyCode(bool enable)
{
    GetJustMyCode() = enable;
}

HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                           std::vector<uint32_t> &hitBreakpointIds)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    if (GetFuncBreakpoints().empty())
    {
        return S_FALSE; // Stopped at a break, but no breakpoints.
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

    std::ostringstream ssParams;
    ssParams << "(";
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

            std::string displayTypeName;
            IfFailRet(MetadataHelpers::GetFQDisplayTypeName(trValue, displayTypeName));
            if (i > 0)
            {
                ssParams << ",";
            }

            ssParams << displayTypeName;
        }
    }
    ssParams << ")";
    const std::string params = ssParams.str();

    // Note, since IsEnableByCondition() during eval execution could neuter the frame, all frame-related calculations
    // must be done before entering this loop.
    for (auto &functionBreakpoints : GetFuncBreakpoints())
    {
        ManagedFunctionBreakpoint &fbp = functionBreakpoints.second;

        if (!fbp.params.empty() && params != fbp.params)
        {
            continue;
        }

        for (auto &[trFuncBreakpoint, nativeAddress] : fbp.trFuncBreakpoints)
        {
            if (FAILED(Status = BreakpointHelpers::IsSameFunctionBreakpoint(trFunctionBreakpoint, trFuncBreakpoint)) ||
                Status == S_FALSE)
            {
                continue;
            }

            CORDB_ADDRESS currentNativeAddress = 0;
            if (SUCCEEDED(BreakpointHelpers::GetBreakpointNativeAddress(trFunctionBreakpoint, currentNativeAddress)) &&
                currentNativeAddress != nativeAddress)
            {
                nativeAddress = currentNativeAddress;
                Breakpoint breakpoint;
                fbp.ToBreakpoint(breakpoint);
                breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
            }

            if (!fbp.condition.empty())
            {
                std::string output;
                if (FAILED(Status = BreakpointHelpers::IsEnableByCondition(pThread, fbp.condition, output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    fbp.ToBreakpoint(breakpoint);
                    breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The condition for a breakpoint failed to evaluate. The condition was '"
                    << fbp.condition << "'. The error returned was '" << output << "'. - "
                    << fbp.name << "(" << fbp.params << ")\n";
                    breakpoint.message = ss.str();
                    DAPIO::EmitOutputEvent({OutputCategory::StdErr, breakpoint.message});
                    DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
                    fbp.condition.clear();
                }
            }

            ++fbp.hitCount;

            if (!fbp.hitCondition.empty())
            {
                std::string output;
                std::ostringstream condstream;
                condstream << fbp.hitCount << ">" << fbp.hitCondition;
                if (FAILED(Status = BreakpointHelpers::IsEnableByCondition(pThread, condstream.str(), output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    fbp.ToBreakpoint(breakpoint);
                    breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The hitCondition for a breakpoint failed to evaluate. The hitCondition was '"
                    << fbp.hitCondition << "'. The error returned was '" << output << "'. - "
                    << fbp.name << "(" << fbp.params << ")\n";
                    breakpoint.message = ss.str();
                    DAPIO::EmitOutputEvent({OutputCategory::StdErr, breakpoint.message});
                    DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
                    fbp.hitCondition.clear();
                }
            }

            hitBreakpointIds.emplace_back(fbp.id);
        }
    }

    return hitBreakpointIds.empty() ? S_FALSE : S_OK; // S_FALSE - stopped at a break, but the breakpoint was not found.
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    for (auto &functionBreakpoints : GetFuncBreakpoints())
    {
        ManagedFunctionBreakpoint &fb = functionBreakpoints.second;

        if (FAILED(ResolveFunctionBreakpointInModule(pModule, fb)))
        {
            continue;
        }

        Breakpoint breakpoint;
        fb.ToBreakpoint(breakpoint);
        DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
    }

    return S_OK;
}

HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    for (auto &functionBreakpoints : GetFuncBreakpoints())
    {
        ManagedFunctionBreakpoint &fb = functionBreakpoints.second;

        if (!fb.IsVerified())
        {
            continue;
        }

        for (auto it = fb.trFuncBreakpoints.begin(); it != fb.trFuncBreakpoints.end();)
        {
            CORDB_ADDRESS brModAddress = 0;
            if (FAILED(BreakpointHelpers::GetFunctionBreakpointModAddress(it->first, brModAddress)) ||
                modAddress != brModAddress)
            {
                ++it;
            }
            else
            {
                BreakpointHelpers::DeactivateManagedBreakpoint(it->first);
                it = fb.trFuncBreakpoints.erase(it);
            }
        }

        if (!fb.IsVerified())
        {
            fb.hitCount = 0;

            Breakpoint breakpoint;
            breakpoint.id = fb.id;
            breakpoint.verified = false;
            breakpoint.message = "Breakpoint reset at module unload.";
            DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
        }
    }

    return S_OK;
}

HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    // Remove old breakpoints
    std::unordered_set<std::string> funcBreakpointFuncs;
    for (const auto &fb : functionBreakpoints)
    {
        const std::string fullFuncName = fb.func + fb.params;
        funcBreakpointFuncs.insert(fullFuncName);
    }
    for (auto it = GetFuncBreakpoints().begin(); it != GetFuncBreakpoints().end();)
    {
        if (funcBreakpointFuncs.find(it->first) == funcBreakpointFuncs.cend())
        {
            Breakpoint breakpoint;
            it->second.ToBreakpoint(breakpoint);
            const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
            DAPIO::EmitBreakpointEvent(event);

            it = GetFuncBreakpoints().erase(it);
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

    // Note, DAP requires that "sourceBreakpoints" and "functionBreakpoints" must have the same indexes for the same breakpoints.

    for (const auto &fb : functionBreakpoints)
    {
        const std::string fullFuncName = fb.func + fb.params;
        Breakpoint breakpoint;

        const auto b = GetFuncBreakpoints().find(fullFuncName);
        if (b == GetFuncBreakpoints().cend())
        {
            // New function breakpoint
            ManagedFunctionBreakpoint fbp;
            fbp.id = getId();
            fbp.name = fb.func;
            fbp.params = fb.params;
            fbp.condition = fb.condition;
            fbp.hitCondition = fb.hitCondition;

            if (haveProcess)
            {
                ResolveFunctionBreakpoint(fbp);
            }

            fbp.ToBreakpoint(breakpoint);
            GetFuncBreakpoints().insert(std::make_pair(fullFuncName, std::move(fbp)));
        }
        else
        {
            ManagedFunctionBreakpoint &fbp = b->second;

            const bool changedCondition = fbp.condition != fb.condition;
            const bool changedHitCondition = fbp.hitCondition != fb.hitCondition;
            fbp.condition = fb.condition;
            fbp.hitCondition = fb.hitCondition;
            fbp.ToBreakpoint(breakpoint);
            if (changedCondition || changedHitCondition)
            {
                if (changedCondition)
                {
                    breakpoint.message = "Breakpoint condition changed.";
                }
                else
                {
                    breakpoint.message = "Breakpoint hitCondition changed.";
                }
                const BreakpointEvent event(BreakpointEventReason::Changed, breakpoint);
                DAPIO::EmitBreakpointEvent(event);
                breakpoint.message.clear();
            }
        }

        breakpoints.push_back(breakpoint);
    }

    return S_OK;
}

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    size_t count = 0;

    for (const auto &functionBreakpoints : GetFuncBreakpoints())
    {
        count += functionBreakpoints.second.trFuncBreakpoints.size();
    }

    return count;
}
#endif // DEBUG_INTERNAL_TESTS

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    // Reset only the parts changed by the process.
    for (auto &functionBreakpoints : GetFuncBreakpoints())
    {
        functionBreakpoints.second.Reset();
    }
}

} // namespace dncdbg::FunctionBreakpoints
