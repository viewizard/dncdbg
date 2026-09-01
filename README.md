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
      <td align="center">Commercial<sup>1</sup></td>
    </tr>
    <tr>
      <td align="left"><b>Implementation</b></td>
      <td align="center">C++</td>
      <td align="center">C++ and C#<sup>2</sup></td>
      <td align="center">C#</td>
    </tr>
    <tr>
      <td align="left"><b>Physical memory footprint<sup>3</sup></b></td>
      <td align="center">11.7M</td>
      <td align="center">52.4M</td>
      <td align="center">146.8M</td>
    </tr>
    <tr>
      <td align="left"><b>Cross Platform</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup>4</sup></td>
      <td align="center">⚠️<sup>5</sup></td>
    </tr>
    <tr>
      <td align="left"><b>Display method parameters in stack trace</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Display local constants (literals)</b></td>
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
      <td align="left"><b>DebuggerBrowsable attribute</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup>6</sup></td>
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
      <td align="left"><b>Tracepoints/Logpoints</b></td>
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
      <td align="left"><b>Module unload</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
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
      <td align="center">⚠️<sup>7</sup></td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="left"><b>Hot Reload</b></td>
      <td align="center">❌</td>
      <td align="center">⚠️<sup>8</sup></td>
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

<small><sup>1</sup> From the VsDbg console output: "You may only use the Microsoft .NET Core Debugger (vsdbg) with Visual Studio Code, Visual Studio or Visual Studio for Mac software to help you develop and test your applications."</small><br>
<small><sup>2</sup> Used only for the symbol reader, the C# expression parser, and primitive type evaluation.</small><br>
<small><sup>3</sup> Measured on macOS 26.6.2 using the `vmmap` utility on a simple application stopped at a breakpoint, with approximately 20 local variables and 10 evaluation requests. The debug session was started inside the VS Code IDE using the DAP protocol; NetCoreDbg was built without the interop and Hot Reload features.</small><br>
<small><sup>4</sup> Does not support `musl`-based Linux distros.</small><br>
<small><sup>5</sup> Does not support `musl`-based Linux arm32 distros.</small><br>
<small><sup>6</sup> Only the `Never` state for properties is supported.</small><br>
<small><sup>7</sup> Linux and Tizen operating systems only.</small><br>
<small><sup>8</sup> Available via the MI/GDB protocol only, and currently limited to the MSVS Tizen plugin.</small><br>

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
