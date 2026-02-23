// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
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
#include "utils/ioredirect.h"
#include "utils/dbgshim.h"
#include "utils/rwlock.h"
#include "utils/span.h"
#include "utils/torelease.h"
#include <condition_variable>
#include <map>
#include <vector>

namespace dncdbg
{

class Threads;
class Steppers;
class Evaluator;
class EvalWaiter;
class EvalHelpers;
class EvalStackMachine;
class Variables;
class ManagedCallback;
class CallbacksQueue;
class Breakpoints;
class DebugInfo;

enum class ProcessAttachedState
{
    Attached,
    Unattached
};

enum class StartMethod
{
    None,
    Launch,
    Attach
};

enum class DisconnectAction
{
    Default, // Attach -> Detach, Launch -> Terminate
    Terminate,
    Detach
};

class ManagedDebugger
{
  public:

    ManagedDebugger();
    ~ManagedDebugger();

    bool IsJustMyCode() const
    {
        return m_justMyCode;
    }
    void SetJustMyCode(bool enable);
    bool IsStepFiltering() const
    {
        return m_stepFiltering;
    }
    void SetStepFiltering(bool enable);
    void SetEvalFlags(uint32_t evalFlags);

    HRESULT Initialize();
    HRESULT Attach(int pid);
    HRESULT Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
                   const std::map<std::string, std::string> &env, const std::string &cwd, bool stopAtEntry = false);
    HRESULT ConfigurationDone();

    HRESULT Disconnect(DisconnectAction action = DisconnectAction::Default);

    ThreadId GetLastStoppedThreadId();
    HRESULT Continue(ThreadId threadId);
    HRESULT Pause(ThreadId lastStoppedThread);
    HRESULT GetThreads(std::vector<Thread> &threads);
    HRESULT SetSourceBreakpoints(const std::string &filename, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                 std::vector<Breakpoint> &breakpoints);
    HRESULT SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                                   std::vector<Breakpoint> &breakpoints);
    HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                    std::vector<Breakpoint> &breakpoints);
    HRESULT GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                          std::vector<StackFrame> &stackFrames);
    HRESULT StepCommand(ThreadId threadId, StepType stepType);
    HRESULT GetScopes(FrameId frameId, std::vector<Scope> &scopes);
    HRESULT GetVariables(uint32_t variablesReference, VariablesFilter filter, int start, int count,
                         std::vector<Variable> &variables);
    HRESULT Evaluate(FrameId frameId, const std::string &expression, Variable &variable, std::string &output);
    void CancelEvalRunning();
    HRESULT SetVariable(const std::string &name, const std::string &value, uint32_t ref, std::string &output);
    HRESULT SetExpression(FrameId frameId, const std::string &expression, const std::string &value, std::string &output);
    HRESULT GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo);

  private:

    friend class ManagedCallback;
    friend class CallbacksQueue;

    std::mutex m_processAttachedMutex; // Note, in case m_debugProcessRWLock+m_processAttachedMutex, m_debugProcessRWLock must be locked first.
    std::condition_variable m_processAttachedCV;
    ProcessAttachedState m_processAttachedState;

    void NotifyProcessCreated();
    void NotifyProcessExited();
    HRESULT CheckNoProcess();

    std::mutex m_lastStoppedMutex;
    ThreadId m_lastStoppedThreadId;

    void SetLastStoppedThread(ICorDebugThread *pThread);
    void SetLastStoppedThreadId(ThreadId threadId);
    void InvalidateLastStoppedThreadId();

    StartMethod m_startMethod;
    std::string m_execPath;
    std::vector<std::string> m_execArgs;
    std::string m_cwd;
    std::map<std::string, std::string> m_env;
    bool m_isConfigurationDone;

    std::shared_ptr<Threads> m_sharedThreads;
    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<EvalWaiter> m_sharedEvalWaiter;
    std::shared_ptr<EvalHelpers> m_sharedEvalHelpers;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;
    std::shared_ptr<Evaluator> m_sharedEvaluator;
    std::shared_ptr<Variables> m_sharedVariables;
    std::unique_ptr<Steppers> m_uniqueSteppers;
    std::shared_ptr<Breakpoints> m_uniqueBreakpoints;
    std::shared_ptr<CallbacksQueue> m_sharedCallbacksQueue;
    std::unique_ptr<ManagedCallback> m_uniqueManagedCallback;

    RWLock m_debugProcessRWLock;
    ToRelease<ICorDebug> m_trDebug;
    ToRelease<ICorDebugProcess> m_trProcess;

    bool m_justMyCode;
    bool m_stepFiltering;

    void *m_unregisterToken;
    DWORD m_processId;
    std::string m_clrPath;
    dbgshim_t m_dbgshim;
    IORedirectHelper m_ioredirect;

    HRESULT CheckDebugProcess();
    bool HaveDebugProcess();

    static void InputCallback(IORedirectHelper::StreamType, Utility::span<char> text);

    void Cleanup();
    void DisableAllBreakpointsAndSteppers();

    HRESULT GetFrameLocation(ICorDebugFrame *pFrame, ThreadId threadId, FrameLevel level, StackFrame &stackFrame);
    HRESULT GetManagedStackTrace(ICorDebugThread *pThread, ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                                 std::vector<StackFrame> &stackFrames);

    CORDB_ADDRESS PrivateCoreLibModAddress;
    mdMethodDef ExceptionDispatchInfoThrowMethodDef;
    HRESULT FindExceptionDispatchInfoThrow(CORDB_ADDRESS &modAddress, mdMethodDef &methodDef);
    bool IsTopFrameExceptionDispatchInfoThrow(ICorDebugThread *pThread);
    HRESULT GetExceptionStackTrace(ICorDebugThread *pThread, std::string &stackTrace);

    static void StartupCallback(IUnknown *pCordb, void *parameter, HRESULT hr);
    HRESULT StartupCallbackHR;
    HRESULT Startup(IUnknown *punk);
    HRESULT RunIfReady();
    HRESULT RunProcess(const std::string &fileExec, const std::vector<std::string> &execArgs);
    HRESULT AttachToProcess();
    HRESULT DetachFromProcess();
    HRESULT TerminateProcess();
};

} // namespace dncdbg
