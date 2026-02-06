// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "interfaces/types.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dncdbg
{

class Evaluator;
class Variables;
class Modules;
class BreakBreakpoint;
class EntryBreakpoint;
class ExceptionBreakpoints;
class FuncBreakpoints;
class LineBreakpoints;

class Breakpoints
{
  public:

    Breakpoints(std::shared_ptr<Modules> &sharedModules, std::shared_ptr<Evaluator> &sharedEvaluator, std::shared_ptr<Variables> &sharedVariables);

    void SetJustMyCode(bool enable);
    void SetLastStoppedIlOffset(ICorDebugProcess *pProcess, const ThreadId &lastStoppedThreadId);
    void SetStopAtEntry(bool enable);
    void DeleteAll();
    HRESULT DisableAll(ICorDebugProcess *pProcess);

    HRESULT SetFuncBreakpoints(bool haveProcess, const std::vector<FuncBreakpoint> &funcBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
    HRESULT SetLineBreakpoints(bool haveProcess, const std::string &filename, const std::vector<LineBreakpoint> &lineBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
    HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints, std::vector<Breakpoint> &breakpoints);

    HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId);
    HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                      std::vector<BreakpointEvent> &bpChangeEvents,  bool &atEntry);
    HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType,
                                     const std::string &excModule);
    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events);
    HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread);

  private:

    std::shared_ptr<BreakBreakpoint> m_breakBreakpoint;
    std::shared_ptr<EntryBreakpoint> m_entryBreakpoint;
    std::shared_ptr<ExceptionBreakpoints> m_exceptionBreakpoints;
    std::shared_ptr<FuncBreakpoints> m_funcBreakpoints;
    std::shared_ptr<LineBreakpoints> m_lineBreakpoints;

    std::mutex m_nextBreakpointIdMutex;
    uint32_t m_nextBreakpointId;
};

} // namespace dncdbg
