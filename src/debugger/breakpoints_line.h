// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include "cor.h"
#include "cordebug.h"

#include "debugger/manageddebugger.h"
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
class Modules;

class LineBreakpoints
{
  public:

    LineBreakpoints(std::shared_ptr<Modules> &sharedModules, std::shared_ptr<Variables> &sharedVariables)
        : m_sharedModules(sharedModules),
          m_sharedVariables(sharedVariables),
          m_justMyCode(true)
    {
    }

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetLineBreakpoints(bool haveProcess, const std::string &filename, const std::vector<LineBreakpoint> &lineBreakpoints,
                               std::vector<Breakpoint> &breakpoints, std::function<uint32_t()> getId);

    // Important! Must provide succeeded return code:
    // S_OK - breakpoint hit
    // S_FALSE - no breakpoint hit
    HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint, Breakpoint &breakpoint,
                               std::vector<BreakpointEvent> &bpChangeEvents);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);

    struct ManagedLineBreakpoint
    {
        uint32_t id;
        std::string module;
        CORDB_ADDRESS modAddress;
        int linenum;
        int endLine;
        ULONG32 times;
        std::string condition;
        // In case of code line in constructor, we could resolve multiple methods for breakpoints.
        // For example, `MyType obj = new MyType(1);` code will be added to all class constructors).
        std::vector<ToRelease<ICorDebugFunctionBreakpoint>> iCorFuncBreakpoints;

        bool IsVerified() const
        {
            return !iCorFuncBreakpoints.empty();
        }

        ManagedLineBreakpoint()
            : id(0),
              modAddress(0),
              linenum(0),
              endLine(0),
              times(0)
        {
        }

        ~ManagedLineBreakpoint()
        {
            for (auto &iCorFuncBreakpoint : iCorFuncBreakpoints)
            {
                if (iCorFuncBreakpoint)
                    iCorFuncBreakpoint->Activate(FALSE);
            }
        }

        void ToBreakpoint(Breakpoint &breakpoint, const std::string &fullname);

        ManagedLineBreakpoint(ManagedLineBreakpoint &&that) = default;
        ManagedLineBreakpoint(const ManagedLineBreakpoint &that) = delete;
        ManagedLineBreakpoint &operator=(ManagedLineBreakpoint &&that) = default;
        ManagedLineBreakpoint &operator=(const ManagedLineBreakpoint &that) = delete;
    };

  private:

    std::shared_ptr<Modules> m_sharedModules;
    std::shared_ptr<Variables> m_sharedVariables;
    bool m_justMyCode;

    struct ManagedLineBreakpointMapping
    {
        LineBreakpoint breakpoint;
        uint32_t id;
        unsigned resolved_fullname_index;
        int resolved_linenum; // if int is 0 - no resolved breakpoint available in m_lineResolvedBreakpoints

        ManagedLineBreakpointMapping()
            : breakpoint("", 0, ""),
              id(0),
              resolved_fullname_index(0),
              resolved_linenum(0)
        {
        }
        ~ManagedLineBreakpointMapping() = default;
    };

    std::mutex m_breakpointsMutex;
    // Resolved line breakpoints:
    // Mapped in order to fast search with mapping data (see container below):
    // resolved source full path index -> resolved line number -> list of all ManagedLineBreakpoint resolved to this line.
    std::unordered_map<unsigned, std::unordered_map<int, std::list<ManagedLineBreakpoint>>> m_lineResolvedBreakpoints;
    // Mapping for input LineBreakpoint array (input from protocol) to ManagedLineBreakpoint or unresolved breakpoint.
    // Note, instead of FuncBreakpoint for resolved breakpoint we could have changed source path and/or line number.
    // In this way we could connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
    // Container have structure for fast compare current breakpoints data with new breakpoints data from protocol:
    // path to source -> list of ManagedLineBreakpointMapping that include LineBreakpoint (from protocol) and resolve related data.
    std::unordered_map<std::string, std::list<ManagedLineBreakpointMapping>> m_lineBreakpointMapping;

};

} // namespace dncdbg
