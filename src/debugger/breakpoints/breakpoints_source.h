// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

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
          m_sharedVariables(sharedVariables),
          m_justMyCode(true)
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
    HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint, std::vector<BreakpointEvent> &bpChangeEvents);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);

    struct ManagedSourceBreakpoint
    {
        uint32_t id;
        std::string module;
        CORDB_ADDRESS modAddress = 0;
        int linenum;
        int endLine;
        uint32_t hitCount;
        std::string condition;
        // In case of code line in constructor, we could resolve multiple methods for breakpoints.
        // For example, `MyType obj = new MyType(1);` code will be added to all class constructors).
        std::vector<ToRelease<ICorDebugFunctionBreakpoint>> iCorFuncBreakpoints;

        bool IsVerified() const
        {
            return !iCorFuncBreakpoints.empty();
        }

        ManagedSourceBreakpoint()
            : id(0),
              modAddress(0),
              linenum(0),
              endLine(0),
              hitCount(0)
        {
        }

        ~ManagedSourceBreakpoint()
        {
            for (auto &iCorFuncBreakpoint : iCorFuncBreakpoints)
            {
                if (iCorFuncBreakpoint)
                    iCorFuncBreakpoint->Activate(FALSE);
            }
        }

        void ToBreakpoint(Breakpoint &breakpoint, const std::string &fullname) const;

        ManagedSourceBreakpoint(ManagedSourceBreakpoint &&that) = default;
        ManagedSourceBreakpoint(const ManagedSourceBreakpoint &that) = delete;
        ManagedSourceBreakpoint &operator=(ManagedSourceBreakpoint &&that) = default;
        ManagedSourceBreakpoint &operator=(const ManagedSourceBreakpoint &that) = delete;
    };

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<Variables> m_sharedVariables;
    bool m_justMyCode;

    struct ManagedSourceBreakpointMapping
    {
        SourceBreakpoint breakpoint;
        uint32_t id;
        unsigned resolved_fullname_index;
        int resolved_linenum; // if int is 0 - no resolved breakpoint available in m_lineResolvedBreakpoints

        ManagedSourceBreakpointMapping()
            : breakpoint(0, ""),
              id(0),
              resolved_fullname_index(0),
              resolved_linenum(0)
        {
        }
        ~ManagedSourceBreakpointMapping() = default;
    };

    std::mutex m_breakpointsMutex;
    // Resolved line breakpoints:
    // Mapped in order to fast search with mapping data (see container below):
    // resolved source full path index -> resolved line number -> list of all ManagedSourceBreakpoint resolved to this line.
    std::unordered_map<unsigned, std::unordered_map<int, std::list<ManagedSourceBreakpoint>>> m_lineResolvedBreakpoints;
    // Mapping for input SourceBreakpoint array (input from protocol) to ManagedSourceBreakpoint or unresolved breakpoint.
    // Note, instead of FunctionBreakpoint for resolved breakpoint we could have changed source path and/or line number.
    // In this way we could connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
    // Container have structure for fast compare current breakpoints data with new breakpoints data from protocol:
    // path to source -> list of ManagedSourceBreakpointMapping that include SourceBreakpoint (from protocol) and resolve related data.
    std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> m_sourceBreakpointMapping;

};

} // namespace dncdbg
