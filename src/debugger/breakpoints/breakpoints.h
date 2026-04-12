// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dncdbg
{

class Evaluator;
class Variables;
class DebugInfo;
class BreakBreakpoint;
class EntryBreakpoint;
class ExceptionBreakpoints;
class FunctionBreakpoints;
class SourceBreakpoints;

class Breakpoints
{
  public:

    Breakpoints(std::shared_ptr<DebugInfo> &sharedDebugInfo, std::shared_ptr<Evaluator> &sharedEvaluator, std::shared_ptr<Variables> &sharedVariables);

    void SetJustMyCode(bool enable);
    void SetLastStoppedIlOffset(ICorDebugProcess *pProcess, const ThreadId &lastStoppedThreadId);
    void SetStopAtEntry(bool enable);
    void DeleteAll();
    static HRESULT DisableAll(ICorDebugProcess *pProcess);

    HRESULT SetFunctionBreakpoints(bool haveProcess, const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                   std::vector<Breakpoint> &breakpoints);
    HRESULT SetSourceBreakpoints(bool haveProcess, const std::string &filename, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                 std::vector<Breakpoint> &breakpoints);
    HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints, std::vector<Breakpoint> &breakpoints);

    HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo);

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
    HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId);
    HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                      std::vector<uint32_t> &hitBreakpointIds,
                                      std::vector<BreakpointEvent> &bpChangeEvents,  bool &atEntry);
    HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType);
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);
    HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);
    HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread);

    static HRESULT ActivateManagedBreakpoint(CORDB_ADDRESS modAddress, uint32_t methodToken, uint32_t ilOffset,
                                             ICorDebugModule *pModule, ICorDebugFunctionBreakpoint **ppFuncBreakpoint);
    static HRESULT DeactivateManagedBreakpoint(ToRelease<ICorDebugFunctionBreakpoint> &trFuncBreakpoint);

  private:

    std::shared_ptr<BreakBreakpoint> m_breakBreakpoint;
    std::shared_ptr<EntryBreakpoint> m_entryBreakpoint;
    std::shared_ptr<ExceptionBreakpoints> m_exceptionBreakpoints;
    std::shared_ptr<FunctionBreakpoints> m_funcBreakpoints;
    std::shared_ptr<SourceBreakpoints> m_sourceBreakpoints;

    std::mutex m_nextBreakpointIdMutex;
    uint32_t m_nextBreakpointId{1};

    struct BreakpointLocation
    {
        CORDB_ADDRESS modAddress{0};
        uint32_t methodToken{0};
        uint32_t ilOffset{0};

        BreakpointLocation(CORDB_ADDRESS modAddress_, uint32_t methodToken_, uint32_t ilOffset_)
            : modAddress(modAddress_),
              methodToken(methodToken_),
              ilOffset(ilOffset_)
        {
        }

        bool operator==(const BreakpointLocation &other) const
        {
            return modAddress == other.modAddress &&
                   methodToken == other.methodToken &&
                   ilOffset == other.ilOffset;
        }
    };

    struct BreakpointLocationHash
    {
        std::size_t operator()(const BreakpointLocation &key) const
        {
            const std::size_t h1 = std::hash<CORDB_ADDRESS>{}(key.modAddress);
            const std::size_t h2 = std::hash<uint32_t>{}(key.methodToken);
            const std::size_t h3 = std::hash<uint32_t>{}(key.ilOffset);
            // Combine hashes using XOR and bit shifting (similar to boost::hash_combine)
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct BreakpointData
    {
        ToRelease<ICorDebugFunctionBreakpoint> trBreakpoint;
        size_t refCount{0};

        BreakpointData(ICorDebugFunctionBreakpoint *pBreakpoint, size_t initialCount)
            : trBreakpoint(pBreakpoint),
              refCount(initialCount)
        {
        }

        BreakpointData(BreakpointData &&) = default;
        BreakpointData(const BreakpointData &) = delete;
        BreakpointData &operator=(BreakpointData &&) = default;
        BreakpointData &operator=(const BreakpointData &) = delete;
        ~BreakpointData() = default;
    };

    static std::mutex &GetManagedBreakpointsMutex()
    {
        static std::mutex managedBreakpointsMutex;
        return managedBreakpointsMutex;
    }

    using mbp_t = std::unordered_map<BreakpointLocation, BreakpointData, BreakpointLocationHash>;
    static mbp_t &GetManagedBreakpoints()
    {
        static mbp_t managedBreakpoints;
        return managedBreakpoints;
    }

};

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_H
