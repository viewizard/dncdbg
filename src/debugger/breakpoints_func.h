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
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dncdbg
{

class Variables;
class Modules;

class FuncBreakpoints
{
  public:

    FuncBreakpoints(std::shared_ptr<Modules> &sharedModules, std::shared_ptr<Variables> &sharedVariables)
        : m_sharedModules(sharedModules),
          m_sharedVariables(sharedVariables),
          m_justMyCode(true)
    {}

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetFuncBreakpoints(bool haveProcess, const std::vector<FuncBreakpoint> &funcBreakpoints,
                               std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);

    // Important! Must provide succeeded return code:
    // S_OK - breakpoint hit
    // S_FALSE - no breakpoint hit
    HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                               std::vector<BreakpointEvent> &bpChangeEvents);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);

  private:

    std::shared_ptr<Modules> m_sharedModules;
    std::shared_ptr<Variables> m_sharedVariables;
    bool m_justMyCode;

    struct ManagedFuncBreakpoint
    {
        uint32_t id;
        std::string module;
        bool module_checked; // in case "module" provided, we need mark that module was checked or not (since function could be not found by name)
        std::string name;
        std::string params;
        ULONG32 hitCount;
        std::string condition;
        std::vector<ToRelease<ICorDebugFunctionBreakpoint>> iCorFuncBreakpoints;

        bool IsResolved() const
        {
            return module_checked;
        }
        bool IsVerified() const
        {
            return !iCorFuncBreakpoints.empty();
        }

        ManagedFuncBreakpoint()
            : id(0),
              module_checked(false),
              hitCount(0)
        {}

        ~ManagedFuncBreakpoint()
        {
            for (auto &iCorFuncBreakpoint : iCorFuncBreakpoints)
            {
                if (iCorFuncBreakpoint)
                    iCorFuncBreakpoint->Activate(FALSE);
            }
        }

        void ToBreakpoint(Breakpoint &breakpoint) const;

        ManagedFuncBreakpoint(ManagedFuncBreakpoint &&that) = default;
        ManagedFuncBreakpoint(const ManagedFuncBreakpoint &that) = delete;
        ManagedFuncBreakpoint &operator=(ManagedFuncBreakpoint &&that) = default;
        ManagedFuncBreakpoint &operator=(const ManagedFuncBreakpoint &that) = delete;
    };

    std::mutex m_breakpointsMutex;
    std::unordered_map<std::string, ManagedFuncBreakpoint> m_funcBreakpoints;

    typedef std::vector<std::pair<ICorDebugModule *, mdMethodDef>> ResolvedFBP;
    HRESULT AddFuncBreakpoint(ManagedFuncBreakpoint &fbp, ResolvedFBP &fbpResolved);
    HRESULT ResolveFuncBreakpointInModule(ICorDebugModule *pModule, ManagedFuncBreakpoint &fbp);
    HRESULT ResolveFuncBreakpoint(ManagedFuncBreakpoint &fbp);
};

} // namespace dncdbg
