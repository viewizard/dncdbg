# Debugger for the .NET Core Runtime

**DNCDbg** is an acronym of .NET Core (dot net core) Debugger. The **DNCDbg** debugger implements [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), allowing the debugging of .NET apps under the .NET Core runtime.

Initially based on [NetCoreDbg](https://github.com/Samsung/netcoredbg) version 3.1.3-1 codebase. The main differences with [NetCoreDbg](https://github.com/Samsung/netcoredbg) are more flexible development and more carefull [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) support in exchange of removal other protocols and some features (mainly Tizen OS related, that can't be used out of MSVS Tizen plugin, etc).

If you need more feature reach debugger for .NET Core runtime, please, take a look at [NetCoreDbg](https://github.com/Samsung/netcoredbg).

## Copyright

You can find licensing information in the [LICENSE](LICENSE) file within the root directory of the DNCDbg sources.

## Building from Source Code

- [Windows OS build.](docs/build_windows.md)
- [Linux OS build.](docs/build_linux.md)

## Usage

TODO
- VSCode IDE, local debugging, Windows OS.
- VSCode IDE, local debugging, Linux OS.
- VSCode IDE, remote debugging, Linux OS.
