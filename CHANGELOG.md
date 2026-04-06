## [Unreleased]
Upcoming changes compared to [NetCoreDbg](https://github.com/Samsung/netcoredbg) version 3.1.3 codebase.

#### DAP
- Added `allowImplicitFuncEval` configuration option support in Launch Request (analog MSVS option: `Enable property evaluation and other implicit function calls`) https://github.com/OmniSharp/omnisharp-vscode/issues/3173
- Added `hitBreakpointIds` support in Stopped Event.
- Added `hitCondition` support for SourceBreakpoint and FunctionBreakpoint.
- Added `isOptimized` support in Module.
- Added `isUserCode` support in Module.
- Added `symbolFilePath` support in Module.
- Added Module Event with `removed` reason on module unload.
- Added Process Event with `attach` start method on attach to debuggee process.
- Added `pointerSize` support in Process Event.
- Added `addressRange` support in Module.
- Added Modules Request support.
- Added `removed` reason support in Breakpoint Event.
- Added Breakpoint Event on changed breakpoint `condition` or `hitCondition`.
- Fixed Cancel Request, `requestId` is optional parameter now.

#### Added
- Added TestStdIO.
- Added TestModules.
- Added tests for Release build.
- Added shrunk [diagnostics](https://github.com/dotnet/diagnostics) sources v9.0.661903, dbgshim library build now during debugger build.
- Added clang-tidy checks.
- Added cppcheck checks.
- Added StartupCallback error processing code.
- Added case-insensitive file name collision for all OSes.
- Added methods parameters output in stacktrace.
- Added active CLR internal frames output in stacktrace.
- Added proper Just My Code enabled stacktrace.
- Added source and function breakpoints reset during module unload.
- Added `--loglevel` launch option for setup minimal log level output.
- Added end-pointer bounds checking to metadata signature parsing.
- Added local constant (literal) variable evaluation implementation.

#### Changed
- Replaced VSCode to DAP (variables, class names, tests, etc).
- Refactored test-suite.
- nlohmann/json version bump to 3.12.0
- Improved and refactored debugger source code.
- Updated package references for managed part.
- Switched to C++17 standard.
- Launch option `engineLogging` renamed to `logProtocol`.
- Managed unwinder will ignore fails on particular frames now and continue unwind.
- Improved managed class constructors related logic for source breakpoints.

#### Removed
- Removed debug build support for .NET Core 2.1.
- Removed build dependency from runtime/coreclr sources.
- Removed getvscodecmd tool.
- Removed MI/GDB and CLI protocols and tests.
- Removed Tizen OS support (rpm build routines, scripts, dlog logging, etc).
- Removed interop debugger parts (this part was proof of concept, not really sure when it will be usable in netcoredbg).
- Removed linenoise from third_party.
- Removed GenErrMsg build.
- Removed Hot Reload feature (since it works only with MI/GDB protocol with MSVS Tizen plugin).
- Removed unused code.
- Removed code duplication in test-suite.
- Removed iprotocol interface (since debugger have only one protocol now).
- Removed idebug interface.
- Removed PAL_STDCPP_COMPAT related code (since it removed from runtime/diagnostics sources now).
- Removed server mode, removed `server` launch option.
- Removed launch options `interpreter`, `command`, `run` and `attach`.
- Removed string_view implementation (switched to std::string_view).
- Removed rwlock implementation (switched to std::shared_mutex).
- Removed escaped string code (nlohmann/json have it implemented now).
- Removed wrong assertion `startLine != other.startLine || startColumn != other.startColumn' (C# record classes related issue).
- Removed Utility::Size() implementation (switched to std::size()).
- Removed span implementation (switched to gsl::span).

#### Fixed
- Fixed extra qualification on Evaluator methods.
- Fixed C++ reserved names usage in code (two underscores usage as name prefix).
- Fixed coding style to Microsoft with clang-format.
- Fixed clang warnings [-Wnontrivial-memcall].
- Fixed error C4242 in Windows build.
- Fixed code performance (removed object copying).
- Fixed header include cycle.
- Fixed logic bug in TryParseSlotIndex method.
- Fixed bug in corhost related logic (TPA list creation).
- Fixed some methods `void *&` (`PVOID &`) parameters to `void **`.
- Fixed stacktrace for exception in async methods (exception rethrow with `System.Runtime.ExceptionServices.ExceptionDispatchInfo.Throw()`).
- Fixed exception type name fail handling logic in GetExceptionDetails().
- Fixed disable JIT optimization related logic.
- Fixed constant field (literal) evaluation logic.
- Fixed entry breakpoint logic, will not double break in case some source breakpoint is also set to first line of Main() method.
- Fixed function breakpoint logic, will not double break in case some source breakpoint is also set to first line of method.
- Fixed undefined behavior in evaluation code.
