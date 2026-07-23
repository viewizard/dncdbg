// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace dncdbg
{

class Evaluator;
class EvalStackMachine;
class DebugInfo;

class SourceBreakpoints
{
  public:

    SourceBreakpoints(std::shared_ptr<DebugInfo> &sharedDebugInfo,
                      std::shared_ptr<Evaluator> &sharedEvaluator,
                      std::shared_ptr<EvalStackMachine> &sharedEvalStackMachine)
        : m_sharedDebugInfo(sharedDebugInfo),
          m_sharedEvaluator(sharedEvaluator),
          m_sharedEvalStackMachine(sharedEvalStackMachine)
    {
    }

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetSourceBreakpoints(bool haveProcess, const std::string &sourcePath, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                 std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);

    // Important! Must provide succeeded return code:
    // S_OK - breakpoint hit
    // S_FALSE - no breakpoint hit
    HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                               std::vector<uint32_t> &hitBreakpointIds);

#ifdef DEBUG_INTERNAL_TESTS
    size_t GetBreakpointsCount();
#endif // DEBUG_INTERNAL_TESTS

    // Important! Callback-related methods must control the return of succeeded return codes.
    // Do not allow debugger API to return succeeded (uncontrolled) return codes.
    // Bad:
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
    HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

    struct ManagedSourceBreakpoint
    {
        uint32_t id{0};
        int lineNum{0};
        int endLine{0};
        uint32_t hitCount{0};
        std::string hitCondition;
        std::string condition;
        std::string logMessage;
        // Parses the logMessage string, each entry is a pair: <text, isExpression>.
        std::vector<std::pair<std::string, bool>> logMessageParts;
        // In case of code line in constructor, we could resolve multiple methods for breakpoints.
        // For example, `MyType obj = new MyType(1);` code will be added to all class constructors).
        std::vector<ToRelease<ICorDebugFunctionBreakpoint>> trFuncBreakpoints;

        [[nodiscard]] bool IsVerified() const
        {
            return !trFuncBreakpoints.empty();
        }

        ManagedSourceBreakpoint() = default;
        ~ManagedSourceBreakpoint();

        void ToBreakpoint(Breakpoint &breakpoint, const std::string &sourceFile) const;

        ManagedSourceBreakpoint(ManagedSourceBreakpoint &&) = default;
        ManagedSourceBreakpoint(const ManagedSourceBreakpoint &) = delete;
        ManagedSourceBreakpoint &operator=(ManagedSourceBreakpoint &&) = default;
        ManagedSourceBreakpoint &operator=(const ManagedSourceBreakpoint &) = delete;
    };

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<Evaluator> m_sharedEvaluator;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;
    bool m_justMyCode{true};

    struct ManagedSourceBreakpointMapping
    {
        SourceBreakpoint breakpoint{0, ""};
        uint32_t id{0};
        PDB::GlobalFileIndex resolvedGlobalFileIndex{};
        int resolvedLineNum{0}; // if 0 - no resolved breakpoint available in m_sourceResolvedBreakpoints

        ManagedSourceBreakpointMapping() = default;
        ManagedSourceBreakpointMapping(ManagedSourceBreakpointMapping &&) = default;
        ManagedSourceBreakpointMapping(const ManagedSourceBreakpointMapping &) = delete;
        ManagedSourceBreakpointMapping &operator=(ManagedSourceBreakpointMapping &&) = delete;
        ManagedSourceBreakpointMapping &operator=(const ManagedSourceBreakpointMapping &) = delete;

        ~ManagedSourceBreakpointMapping() = default;
    };

    std::mutex m_breakpointsMutex;
    // Resolved line breakpoints:
    // Mapped for fast search with mapping data (see container below):
    // resolved global source path index -> resolved line number -> list of all ManagedSourceBreakpoint resolved to this line.
    std::unordered_map<PDB::GlobalFileIndex, std::unordered_map<int, std::list<ManagedSourceBreakpoint>>, PDB::GlobalFileIndexHash> m_sourceResolvedBreakpoints;
    // Mapping for input SourceBreakpoint array (input from protocol) to ManagedSourceBreakpoint or unresolved breakpoint.
    // Note, unlike FunctionBreakpoint, for a resolved breakpoint we could have changed source path and/or line number.
    // In this way we can connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
    // Container has structure for fast comparison of current breakpoints data with new breakpoints data from protocol:
    // path to source -> list of ManagedSourceBreakpointMapping that includes SourceBreakpoint (from protocol) and resolve related data.
    std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> m_sourceBreakpointMapping;

};

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_SOURCE_H
