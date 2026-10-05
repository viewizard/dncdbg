// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Note: DEBUGGER_CONFIG_H is used here, since CONFIG_H is quite common.
#ifndef DEBUGGER_CONFIG_H
#define DEBUGGER_CONFIG_H

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>

namespace dncdbg::Config
{

void Initialize();
void Initialize(const std::map<std::string, std::string> &env);

// Maximum number of frames in a stack trace, provided by the DNCDBG_STACKTRACE_LIMIT environment variable.
uint32_t GetStackTraceLimit();
// DAP request execution timeout in milliseconds, provided by the DNCDBG_DAP_REQUEST_TIMEOUT environment variable.
uint32_t GetDapRequestTimeoutMs();
// Normal evaluation timeout in milliseconds, provided by the DNCDBG_NORMAL_EVAL_TIMEOUT environment variable.
uint32_t GetNormalEvalTimeoutMs();
// Abort evaluation timeout in milliseconds, provided by the DNCDBG_ABORT_EVAL_TIMEOUT environment variable.
uint32_t GetAbortEvalTimeoutMs();
// HTTP/HTTPS request timeout in milliseconds, provided by the DNCDBG_HTTP_REQUEST_TIMEOUT environment variable.
uint32_t GetHttpRequestTimeoutMs();
// Maximum number of members per page before a "[More]" entry is added, provided by the DNCDBG_MEMBERS_PER_PAGE_LIMIT environment variable.
uint32_t GetMembersPerPageLimit();
// Startup (launch and attach) timeout in milliseconds, provided by the DNCDBG_STARTUP_TIMEOUT environment variable.
uint32_t GetStartupTimeoutMs();
// Process termination timeout in milliseconds, provided by the DNCDBG_TERMINATION_TIMEOUT environment variable.
uint32_t GetTerminationTimeoutMs();
// Maximum number of members marked with DebuggerBrowsableState.RootHidden that are unwrapped in
// a single walk, provided by the DNCDBG_ROOTHIDDEN_WALK_LIMIT environment variable. Zero disables unwrapping.
uint32_t GetRootHiddenWalkLimit();

// The debugger runs under the VS Code IDE, which passes the "--interpreter=vscode" command-line option.
bool IsRunningViaVsDbgUI();
void SetRunningViaVsDbgUI(bool state);

// Just My Code (JMC) debugger option, provided by the DAP protocol ("launch" request).
bool GetJustMyCode();
void SetJustMyCode(bool state);

// Step filtering debugger option, provided by the DAP protocol ("launch" request).
bool GetStepFiltering();
void SetStepFiltering(bool state);

// Stop at entry point debugger option, provided by the DAP protocol ("launch" request).
bool GetStopAtEntry();
void SetStopAtEntry(bool state);

// Suppress JIT optimizations debugger option, provided by the DAP protocol ("launch" request).
bool GetSuppressJITOptimizations();
void SetSuppressJITOptimizations(bool state);

// Expression evaluation flags, provided by the DAP protocol ("launch" request).
constexpr uint32_t EVAL_DEFAULT       = 0x0000;
constexpr uint32_t EVAL_NOFUNCEVAL    = 0x0002;
constexpr uint32_t EVAL_NOTOSTRING    = 0x0004;
constexpr uint32_t EVAL_SHOWRAWVALUES = 0x0008;

uint32_t GetEvalFlags();
void SetEvalFlags(uint32_t evalFlags);

} // namespace dncdbg::Config

#endif // DEBUGGER_CONFIG_H
