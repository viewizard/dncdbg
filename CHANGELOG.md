## [Unreleased]
Upcoming changes compared to [NetCoreDbg](https://github.com/Samsung/netcoredbg) version 3.1.3 codebase.

#### Added
- Added TestStdIO.
- Added diagnostics sources, dbgshim library will build now during debugger build.
- Added clang-tidy checks.
- Added StartupCallback error processing code.
- Added case-insensitive file name collision for all OSes.

#### Changed
- Replaced VSCode to DAP (variables, class names, tests, etc).
- Refactored test-suite.
- nlohmann/json version bump to 3.12.0
- Refactored filesystem related code.
- Refactored interop related code.
- Refactored EmitOutputEvent method.
- Improved NameForTypeSig method code.
- Updated package references for managed part.
- Switched to C++17 standard.
- Launch option `engineLogging` renamed to `logProtocol`.

#### Removed
- Removed debug build support for .NET Core 2.1.
- Removed getvscodecmd tool.
- Removed MI/GDB and CLI protocols and tests.
- Removed Tizen OS support (rpm build routines, scripts, dlog logging, etc).
- Removed interop debugger parts (this part was proof of concept, not really sure when it will be usable in netcoredbg).
- Removed linenoise from third_paty.
- Removed GenErrMsg build.
- Removed Hot Reload feature (since it work only with MI/GDB protocol with MSVS Tizen plugin).
- Removed unused code.
- Removed code duplication in test-suite.
- Removed iprotocol interface (since debugger have only one protocol now).
- Removed idebug interface.
- Removed PAL_STDCPP_COMPAT related code (since it removed from runtime/diagnostics sources now).
- Removed server mode, removed `server` launch option.
- Removed launch options `interpreter`, `command`, `run` and `attach`.
- Removed string_view implementation.

#### Fixed
- Fixed extra qualification on Evaluator methods.
- Fixed C++ reserved names usage in code (two underscores usage as name prefix).
- Fixed coding style to Microsoft with clang-format.
- Fixed clang warnings [-Wnontrivial-memcall].
- Fixed error C4242 in Windows build.
- Fixed code performance (removed object copying).
- Fixed header include cycle.
- Fixed logic bug in TryParseSlotIndex method.
