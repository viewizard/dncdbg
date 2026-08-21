# Debugger for the .NET Core Runtime

**DNCDbg** is an acronym for .NET Core (dot net core) Debugger. It implements the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), allowing you to debug .NET applications running on all versions of the .NET runtime.

Initially, DNCDbg was based on version 3.1.3 of the [NetCoreDbg](https://github.com/Samsung/netcoredbg) source code, but unlike NetCoreDbg, its development is fully hosted on GitHub, making it more collaborative and flexible.

**Project goals:** implement functionality close to that of the MSVS C# debugger (excluding features related to the MSVS IDE and those not supported by DAP), provide complete [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) support, and achieve low memory consumption and high performance.

See the current [Debug Adapter Protocol support status](docs/dap_status.md).

|                            |    DNCDbg    |  NetCoreDbg  |  [VsDbg](https://github.com/omnisharp/omnisharp-vscode)  |
|:---------------------------|:------------:|:------------:|:--------------------------------------------------------:|
| **License** | MIT | MIT | Commercial<sup>1</sup> |
| **Implementation** | C++ | C++ and C#<sup>2</sup> | C# |
| **Physical memory footprint<sup>3</sup>** | 37.9M | 52.4M | 146.8M |
| **[Inputting text into the target process](docs/inputting_text.md)** | ✅ | ❌ | ✅ |
| **[Source File Map](https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map)** | ✅ | ❌ | ✅ |
| **Display method parameters in stack trace** | ✅ | ❌ | ✅ |
| **Display in/ref/out parameter modifiers** | ✅ | ❌ | ✅ |
| **Display active CLR internal frames in stack trace** | ✅ | ❌ | ✅ |
| **Module unload** | ✅ | ❌ | ✅ |
| **Constants (literals)** | ✅ | ❌ | ✅ |
| **Embedded PDB** | ✅ | ❌ | ✅ |
| **[Evaluation format specifiers](docs/evaluation_format_specifiers.md)** | ✅ | ❌ | ✅ |
| **DebuggerDisplay attribute** | ✅ | ❌ | ✅ |
| **DebuggerTypeProxy attribute** | ✅ | ❌ | ✅ |
| **Using-directive awareness in type resolution** | ✅ | ❌ | ✅ |
| **Pagination for variable children** | ✅ | ❌ | ✅ |
| **MI/GDB and CLI protocols** | ❌ | ✅ | ❌ |
| **Interop (Mixed) debug** | ❌ | ✅<sup>4</sup> | ❓ |
| **Hot Reload** | ❌ | ✅<sup>5</sup> | ❓ |

<small><sup>1</sup> From the VsDbg console output: "You may only use the Microsoft .NET Core Debugger (vsdbg) with Visual Studio Code, Visual Studio or Visual Studio for Mac software to help you develop and test your applications."</small><br>
<small><sup>2</sup> Used only for the symbol reader, the C# expression parser, and primitive type evaluation.</small><br>
<small><sup>3</sup> Measured on macOS 26.6.2 using a simple application stopped at a breakpoint, with approximately 20 local variables and 10 evaluation requests. The debug session was started inside the VSCode IDE using the DAP protocol; NetCoreDbg was built without the interop and Hot Reload features.</small><br>
<small><sup>4</sup> Linux and Tizen operating systems only.</small><br>
<small><sup>5</sup> Available via the MI/GDB protocol only, and currently limited to the MSVS Tizen plugin.</small>

## Development Process

The project uses Trunk-Based Development, which means you can build the current upstream code and be sure you have the latest version with all features and fixes included. Upcoming changes can be found in [CHANGELOG.md](CHANGELOG.md).

## Contributing

Contributions are welcome! Please read our [Contributing Guidelines](CONTRIBUTING.md) to learn how you can report bugs, propose features, or contribute code changes through forks.

## Building from Source Code

- [Windows OS build.](docs/build_windows.md)
- [Linux OS build.](docs/build_linux.md)
- [macOS build.](docs/build_macos.md)
- [Local testing.](test-suite/README.md)

## Usage

- [VSCode IDE, Windows OS.](docs/usage_vscode_windows.md)
- [VSCode IDE, Linux and macOS.](docs/usage_vscode_unix.md)
- [Debugger pseudo-variables.](docs/pseudo_variables.md)
- [Inputting text into the target process.](docs/inputting_text.md)
- [Evaluation format specifiers.](docs/evaluation_format_specifiers.md)
