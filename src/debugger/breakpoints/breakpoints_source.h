// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dncdbg
{

class Variables;
class DebugInfo;

class SourceBreakpoints
{
  public:

    SourceBreakpoints(std::shared_ptr<DebugInfo> &sharedDebugInfo, std::shared_ptr<Variables> &sharedVariables)
        : m_sharedDebugInfo(sharedDebugInfo),
          m_sharedVariables(sharedVariables)
    {
    }

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetSourceBreakpoints(bool haveProcess, const std::string &filename, const std::vector<SourceBreakpoint> &sourceBreakpoints,
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

    struct ManagedSourceBreakpoint
    {
        uint32_t id{0};
        std::string module;
        int linenum{0};
        int endLine{0};
        uint32_t hitCount{0};
        std::string hitCondition;
        std::string condition;
        // In case of code line in constructor, we could resolve multiple methods for breakpoints.
        // For example, `MyType obj = new MyType(1);` code will be added to all class constructors).
        std::vector<ToRelease<ICorDebugFunctionBreakpoint>> trFuncBreakpoints;

        [[nodiscard]] bool IsVerified() const
        {
            return !trFuncBreakpoints.empty();
        }

        ManagedSourceBreakpoint() = default;
        ~ManagedSourceBreakpoint()
        {
            for (auto &trFuncBreakpoint : trFuncBreakpoints)
            {
                if (trFuncBreakpoint != nullptr)
                {
                    trFuncBreakpoint->Activate(FALSE);
                }
            }
        }

        void ToBreakpoint(Breakpoint &breakpoint, const std::string &fullname) const;

        ManagedSourceBreakpoint(ManagedSourceBreakpoint &&) = default;
        ManagedSourceBreakpoint(const ManagedSourceBreakpoint &) = delete;
        ManagedSourceBreakpoint &operator=(ManagedSourceBreakpoint &&) = default;
        ManagedSourceBreakpoint &operator=(const ManagedSourceBreakpoint &) = delete;
    };

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<Variables> m_sharedVariables;
    bool m_justMyCode{true};

    struct ManagedSourceBreakpointMapping
    {
        SourceBreakpoint breakpoint{0, ""};
        uint32_t id{0};
        unsigned resolved_fullname_index{0};
        int resolved_linenum{0}; // if 0 - no resolved breakpoint available in m_sourceResolvedBreakpoints

        ManagedSourceBreakpointMapping() = default;
        ManagedSourceBreakpointMapping(ManagedSourceBreakpointMapping &&) = default;
        ManagedSourceBreakpointMapping(const ManagedSourceBreakpointMapping &) = delete;
        ManagedSourceBreakpointMapping &operator=(ManagedSourceBreakpointMapping &&) = delete;
        ManagedSourceBreakpointMapping &operator=(const ManagedSourceBreakpointMapping &) = delete;
        
        ~ManagedSourceBreakpointMapping() = default;
    };

    std::mutex m_breakpointsMutex;
    // Resolved line breakpoints:
    // Mapped in order to fast search with mapping data (see container below):
    // resolved source full path index -> resolved line number -> list of all ManagedSourceBreakpoint resolved to this line.
    std::unordered_map<unsigned, std::unordered_map<int, std::list<ManagedSourceBreakpoint>>> m_sourceResolvedBreakpoints;
    // Mapping for input SourceBreakpoint array (input from protocol) to ManagedSourceBreakpoint or unresolved breakpoint.
    // Note, instead of FunctionBreakpoint for resolved breakpoint we could have changed source path and/or line number.
    // In this way we could connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
    // Container have structure for fast compare current breakpoints data with new breakpoints data from protocol:
    // path to source -> list of ManagedSourceBreakpointMapping that include SourceBreakpoint (from protocol) and resolve related data.
    std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> m_sourceBreakpointMapping;

};

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H
