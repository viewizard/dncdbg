// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_MANAGEDDEBUGGER_H
#define DEBUGGER_MANAGEDDEBUGGER_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <gsl/span>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dncdbg::ManagedDebugger
{

// The ManagedDebugger namespace provides the debugger public API for the DAP protocol layer (see dap.cpp).
// Its internal state is initialized by Initialize() and shut down by Shutdown().

enum class DisconnectAction : uint8_t
{
    Default, // Attach -> Detach, Launch -> Terminate
    Terminate,
    Detach
};

// Initializes the ManagedDebugger internal state. Must be called once per debugger lifetime, before any
// other function. Throws std::exception on failure (e.g., dbgshim library loading error).
void Initialize();
// Shuts down the ManagedDebugger internal state; must be called before the process exits.
void Shutdown();

HRESULT Attach(DWORD pid);
HRESULT Launch(const std::string &fileExec, const std::vector<std::string> &execArgs,
               const std::map<std::string, std::string> &env, const std::string &cwd);
HRESULT ConfigurationDone();

HRESULT Disconnect(DisconnectAction action = DisconnectAction::Default);

ThreadId GetLastStoppedThreadId();
HRESULT Continue(ThreadId threadId, bool singleThread);
bool IsProcessRunning();
HRESULT Pause(ThreadId lastStoppedThread);
HRESULT GetThreads(std::vector<Thread> &threads);
HRESULT SetSourceBreakpoints(const Source &source, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints);
HRESULT SetFunctionBreakpoints(const std::vector<FunctionBreakpoint> &functionBreakpoints,
                               std::vector<Breakpoint> &breakpoints);
HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                std::vector<Breakpoint> &breakpoints);
HRESULT GetStackTrace(ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                      std::vector<StackFrame> &stackFrames);
HRESULT StepCommand(ThreadId threadId, StepType stepType, bool singleThread);
HRESULT GetScopes(FrameId frameId, std::vector<Scope> &scopes);
HRESULT GetVariables(uint32_t variablesReference, std::vector<Variable> &variables);
HRESULT Evaluate(FrameId frameId, const std::string &expression, Variable &variable, std::string &output);
void CancelEvalRunning();
HRESULT SetVariable(const std::string &name, const std::string &value, uint32_t ref, std::string &output);
HRESULT SetExpression(FrameId frameId, const std::string &expression, const std::string &value, std::string &output);
HRESULT GetExceptionInfo(ThreadId threadId, ExceptionInfo &exceptionInfo);
void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules);
HRESULT GetGotoTarget(const Source &source, int32_t line, int32_t column,
                      std::vector<GotoTarget> &targets, std::string &output);
HRESULT Goto(ThreadId threadId, uint32_t targetId, std::string &output);
HRESULT GetSourceContent(const Source &source, std::string &sourceContent);
void GetLoadedSources(std::vector<Source> &sources);
HRESULT GetBreakpointLocations(const Source &source, const BreakpointLocation &rangeToSearch,
                               std::vector<BreakpointLocation> &locations);
void SetSourceFileMap(std::map<std::string, std::string> &&map);

void WriteStdin(gsl::span<const char> text);
bool InitializeRemoteConsoleServer(int port);

} // namespace dncdbg::ManagedDebugger

#endif // DEBUGGER_MANAGEDDEBUGGER_H
