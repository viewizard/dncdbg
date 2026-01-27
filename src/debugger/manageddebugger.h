// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#include "cor.h"
#include "cordebug.h"

#include <map>
#include <vector>

#include "debugger/dbgshim.h"
#include "interfaces/types.h"
#include "utils/ioredirect.h"
#include "utils/rwlock.h"
#include "utils/span.h"
#include "utils/torelease.h"

namespace dncdbg
{

template <typename T> using span = Utility::span<T>;

class DAP;
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
class Modules;

enum class ProcessAttachedState
{
    Attached,
    Unattached
};

enum StartMethod
{
    StartNone,
    StartLaunch,
    StartAttach
    // StartAttachForSuspendedLaunch
};

class ManagedDebugger
{
  public:

    enum StepType
    {
        STEP_IN = 0,
        STEP_OVER,
        STEP_OUT
    };

    enum DisconnectAction
    {
        DisconnectDefault, // Attach -> Detach, Launch -> Terminate
        DisconnectTerminate,
        DisconnectDetach
    };

    ManagedDebugger(DAP *pProtocol);
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

    HRESULT Initialize();
    HRESULT Attach(int pid);
    HRESULT Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
                   const std::map<std::string, std::string> &env, const std::string &cwd, bool stopAtEntry = false);
    HRESULT ConfigurationDone();

    HRESULT Disconnect(DisconnectAction action = DisconnectDefault);

    ThreadId GetLastStoppedThreadId();
    HRESULT Continue(ThreadId threadId);
    HRESULT Pause(ThreadId lastStoppedThread, EventFormat eventFormat);
    HRESULT GetThreads(std::vector<Thread> &threads);
    HRESULT SetLineBreakpoints(const std::string &filename, const std::vector<LineBreakpoint> &lineBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
    HRESULT SetFuncBreakpoints(const std::vector<FuncBreakpoint> &funcBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
    HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                    std::vector<Breakpoint> &breakpoints);
    HRESULT GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                          std::vector<StackFrame> &stackFrames, int &totalFrames);
    HRESULT StepCommand(ThreadId threadId, StepType stepType);
    HRESULT GetScopes(FrameId frameId, std::vector<Scope> &scopes);
    HRESULT GetVariables(uint32_t variablesReference, VariablesFilter filter, int start, int count,
                         std::vector<Variable> &variables);
    HRESULT Evaluate(FrameId frameId, const std::string &expression, Variable &variable, std::string &output);
    void CancelEvalRunning();
    HRESULT SetVariable(const std::string &name, const std::string &value, uint32_t ref, std::string &output);
    HRESULT SetExpression(FrameId frameId, const std::string &expression, int evalFlags, const std::string &value,
                          std::string &output);
    HRESULT GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo);

  private:

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

    DAP *pProtocol;
    std::shared_ptr<Threads> m_sharedThreads;
    std::shared_ptr<Modules> m_sharedModules;
    std::shared_ptr<EvalWaiter> m_sharedEvalWaiter;
    std::shared_ptr<EvalHelpers> m_sharedEvalHelpers;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;
    std::shared_ptr<Evaluator> m_sharedEvaluator;
    std::shared_ptr<Variables> m_sharedVariables;
    std::unique_ptr<Steppers> m_uniqueSteppers;
    std::shared_ptr<Breakpoints> m_uniqueBreakpoints;
    std::shared_ptr<CallbacksQueue> m_sharedCallbacksQueue;
    std::unique_ptr<ManagedCallback> m_uniqueManagedCallback;

    Utility::RWLock m_debugProcessRWLock;
    ToRelease<ICorDebug> m_iCorDebug;
    ToRelease<ICorDebugProcess> m_iCorProcess;

    bool m_justMyCode;
    bool m_stepFiltering;

    PVOID m_unregisterToken;
    DWORD m_processId;
    std::string m_clrPath;
    dbgshim_t m_dbgshim;
    IORedirectHelper m_ioredirect;

    HRESULT CheckDebugProcess();
    bool HaveDebugProcess();

    void InputCallback(IORedirectHelper::StreamType, span<char> text);

    void Cleanup();
    void DisableAllBreakpointsAndSteppers();

    HRESULT GetFrameLocation(ICorDebugFrame *pFrame, ThreadId threadId, FrameLevel level, StackFrame &stackFrame);
    HRESULT GetManagedStackTrace(ICorDebugThread *pThread, ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                                 std::vector<StackFrame> &stackFrames, int &totalFrames);

    friend class ManagedCallback;
    friend class CallbacksQueue;

    static VOID StartupCallback(IUnknown *pCordb, PVOID parameter, HRESULT hr);
    HRESULT Startup(IUnknown *punk);
    HRESULT RunIfReady();
    HRESULT RunProcess(const std::string &fileExec, const std::vector<std::string> &execArgs);
    HRESULT AttachToProcess();
    HRESULT DetachFromProcess();
    HRESULT TerminateProcess();
};

} // namespace dncdbg
