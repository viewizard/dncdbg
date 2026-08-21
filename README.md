# Debugger for the .NET Core Runtime

**DNCDbg** is an acronym of .NET Core (dot net core) Debugger. The DNCDbg debugger implements [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), allowing the debugging of .NET apps under the .NET Core runtime.

Initially DNCDbg was based on [NetCoreDbg](https://github.com/Samsung/netcoredbg) version 3.1.3 source code, but in contrast to NetCoreDbg, development is fully based on GitHub to be more collaborative and flexible.

**Project goals:** implement close to MSVS C# debugger functionality (excluding related to MSVS IDE and not DAP supported features), complete [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) support, low memory consumption and high performance.

Current [Debug Adapter Protocol support status](docs/dap_status.md).

|                            |    DNCDbg    |  NetCoreDbg  |
|:---------------------------|:------------:|:------------:|
| **Implementation** | C++ | C++ and C#<sup>2</sup> |
| **Physical memory footprint<sup>1</sup>** | 37.9M | 52.4M |
| **[Inputting text into the target process](docs/inputting_text.md) support** | ✅ | ❌ |
| **[Source File Map](https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map) support** | ✅ | ❌ |
| **Display method parameters in stack trace** | ✅ | ❌ |
| **Display in/ref/out parameter modifiers** | ✅ | ❌ |
| **Display active CLR internal frames in stack trace** | ✅ | ❌ |
| **Module unload support** | ✅ | ❌ |
| **Constants (literals) support** | ✅ | ❌ |
| **Embedded PDB support** | ✅ | ❌ |
| **[Evaluation format specifiers](docs/evaluation_format_specifiers.md) support** | ✅ | ❌ |
| **DebuggerDisplay attribute support** | ✅ | ❌ |
| **DebuggerTypeProxy attribute support** | ✅ | ❌ |
| **Using-directive awareness in type resolution** | ✅ | ❌ |
| **Pagination for variable children** | ✅ | ❌ |
| **MI/GDB and CLI protocols** | ❌ | ✅ |
| **Interop (Mixed) debug** | ❌ | ✅<sup>3</sup> |
| **Hot Reload feature** | ❌ | ✅<sup>4</sup> |

<small><sup>1</sup> Measured on macOS 26.6.2 using a simple application stopped at a breakpoint with approximately 20 local variables and 10 evaluation requests. The debug session was started inside the VS Code IDE using the DAP protocol; NetCoreDbg was built without interop and Hot Reload features.</small><br>
<small><sup>2</sup> Used only for the symbol reader, the C# expression parser, and primitive type evaluation.</small><br>
<small><sup>3</sup> Linux and Tizen operating systems only.</small><br>
<small><sup>4</sup> Available via the MI/GDB protocol only and currently limited to the MSVS Tizen plugin.</small>

## Development process

The project's development strategy is Trunk-Based Development, this means you can build current upstream code and be sure you have last version with all features and fixes included. You can find upcoming changes in [CHANGELOG.md](CHANGELOG.md).

## Building from Source Code

- [Windows OS build.](docs/build_windows.md)
- [Linux OS build.](docs/build_linux.md)
- [MacOS build.](docs/build_macos.md)
- [Local testing.](test-suite/README.md)

## Usage

- [VSCode IDE, Windows OS.](docs/usage_vscode_windows.md)
- [VSCode IDE, Linux and macOS OSes.](docs/usage_vscode_unix.md)
- [Debugger pseudo-variables.](docs/pseudo_variables.md)
- [Inputting text into the target process.](docs/inputting_text.md)
- [Evaluation format specifiers.](docs/evaluation_format_specifiers.md)
