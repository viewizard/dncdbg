# Debugger for the .NET Core Runtime

**DNCDbg** is an acronym for .NET Core (dot net core) Debugger. It implements the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), allowing you to debug .NET applications running on all versions of the .NET runtime.

Initially, DNCDbg was based on version 3.1.3 of the [NetCoreDbg](https://github.com/Samsung/netcoredbg) source code, but unlike NetCoreDbg, its development is fully hosted on GitHub, making it more collaborative and flexible.

**Project goals:** implement functionality close to that of the MSVS C# debugger (excluding features related to the MSVS IDE and those not supported by DAP), provide complete [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) support, and achieve low memory consumption and high performance.

See the current [Debug Adapter Protocol support status](docs/dap_status.md).

<div align="center">

<table>
  <thead>
    <tr>
      <th align="left"></th>
      <th align="center">DNCDbg</th>
      <th align="center"><a href="https://github.com/Samsung/netcoredbg">NetCoreDbg</a></th>
      <th align="center"><a href="https://github.com/dotnet/vscode-csharp">VsDbg</a></th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="left"><b>License</b></td>
      <td align="center">MIT</td>
      <td align="center">MIT</td>
      <td align="center">Commercial<sup><a href="docs/dbg_compare.md#note-1">1</a></sup></td>
    </tr>
    <tr>
      <td align="left"><b>Implementation</b></td>
      <td align="center">C++</td>
      <td align="center">C++ and C#<sup><a href="docs/dbg_compare.md#note-2">2</a></sup></td>
      <td align="center">C#</td>
    </tr>
    <tr>
      <td align="left"><b>Physical memory footprint<sup><a href="docs/dbg_compare.md#note-3">3</a></sup></b></td>
      <td align="center">11.7M</td>
      <td align="center">52.4M</td>
      <td align="center">146.8M</td>
    </tr>
    <tr>
      <td align="left"><b>Cross Platform</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup><a href="docs/dbg_compare.md#note-4">4</a></sup></td>
      <td align="center">⚠️<sup><a href="docs/dbg_compare.md#note-5">5</a></sup></td>
    </tr>
    <tr>
      <td align="left"><b>Display human readable async stack trace</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Use <code>ToString()</code> for object variable display</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>DebuggerBrowsable attribute</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup><a href="docs/dbg_compare.md#note-6">6</a></sup></td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>DebuggerDisplay attribute</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>DebuggerTypeProxy attribute</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><a href="docs/evaluation_format_specifiers.md"><b>Evaluation format specifiers</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Logpoints</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Embedded PDB</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Embedded sources</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><a href="https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map"><b>Source File Map</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><a href="docs/inputting_text.md"><b>Inputting text into the target process</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Jump To Cursor (Goto)</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Single thread execution and stepping</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="left"><b>MI/GDB and CLI protocols</b></td>
      <td align="center">❌</td>
      <td align="center">✅</td>
      <td align="center">❌</td>
    </tr>
    <tr>
      <td align="left"><b>Interop (Mixed) debug</b></td>
      <td align="center">❌</td>
      <td align="center">⚠️<sup><a href="docs/dbg_compare.md#note-7">7</a></sup></td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="left"><b>Hot Reload</b></td>
      <td align="center">❌</td>
      <td align="center">⚠️<sup><a href="docs/dbg_compare.md#note-8">8</a></sup></td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="center" colspan="4">
        <a href="docs/dbg_compare.md">View Full Comparison Table</a>
      </td>
    </tr>
  </tbody>
</table>

</div>

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

- [VS Code IDE, Windows OS.](docs/usage_vscode_windows.md)
- [VS Code IDE, Linux and macOS.](docs/usage_vscode_unix.md)
- [Debugger pseudo-variables.](docs/pseudo_variables.md)
- [Inputting text into the target process.](docs/inputting_text.md)
- [Evaluation format specifiers.](docs/evaluation_format_specifiers.md)
