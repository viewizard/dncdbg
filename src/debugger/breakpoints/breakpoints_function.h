// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <functional>
#include <memory>
#include <mutex>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace dncdbg
{

class Variables;
class DebugInfo;

class FunctionBreakpoints
{
  public:

    FunctionBreakpoints(std::shared_ptr<DebugInfo> &sharedDebugInfo, std::shared_ptr<Variables> &sharedVariables)
        : m_sharedDebugInfo(sharedDebugInfo),
          m_sharedVariables(sharedVariables)
    {}

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                   std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);

    // Important! Must provide succeeded return code:
    // S_OK - breakpoint hit
    // S_FALSE - no breakpoint hit
    HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                               std::vector<uint32_t> &hitBreakpointIds,
                               std::vector<BreakpointEvent> &bpChangeEvents);

#ifdef DEBUG_INTERNAL_TESTS
    size_t GetBreakpointsCount();
#endif // DEBUG_INTERNAL_TESTS

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);
    HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<Variables> m_sharedVariables;
    bool m_justMyCode{true};

    struct ManagedFunctionBreakpoint
    {
        uint32_t id{0};
        std::string name;
        std::string params;
        uint32_t hitCount{0};
        std::string hitCondition;
        std::string condition;
        std::list<ToRelease<ICorDebugFunctionBreakpoint>> trFuncBreakpoints;

        [[nodiscard]] bool IsVerified() const
        {
            return !trFuncBreakpoints.empty();
        }

        ManagedFunctionBreakpoint() = default;
        ~ManagedFunctionBreakpoint()
        {
            for (auto &trFuncBreakpoint : trFuncBreakpoints)
            {
                if (trFuncBreakpoint != nullptr)
                {
                    trFuncBreakpoint->Activate(FALSE);
                }
            }
        }

        void ToBreakpoint(Breakpoint &breakpoint) const;

        ManagedFunctionBreakpoint(ManagedFunctionBreakpoint &&) = default;
        ManagedFunctionBreakpoint(const ManagedFunctionBreakpoint &) = delete;
        ManagedFunctionBreakpoint &operator=(ManagedFunctionBreakpoint &&) = default;
        ManagedFunctionBreakpoint &operator=(const ManagedFunctionBreakpoint &) = delete;
    };

    std::mutex m_breakpointsMutex;
    std::unordered_map<std::string, ManagedFunctionBreakpoint> m_funcBreakpoints;

    using ResolvedFBP = std::vector<std::pair<ICorDebugModule *, mdMethodDef>>;
    HRESULT AddFunctionBreakpoint(ManagedFunctionBreakpoint &fbp, ResolvedFBP &fbpResolved);
    HRESULT ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, ManagedFunctionBreakpoint &fbp);
    HRESULT ResolveFunctionBreakpoint(ManagedFunctionBreakpoint &fbp);
};

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_FUNCTION_H
