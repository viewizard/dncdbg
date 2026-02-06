# Debugger for the .NET Core Runtime

**DNCDbg** is an acronym of .NET Core (dot net core) Debugger. The DNCDbg debugger implements [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), allowing the debugging of .NET apps under the .NET Core runtime.

Initially DNCDbg based on [NetCoreDbg](https://github.com/Samsung/netcoredbg) version 3.1.3 source code, but in contrast to NetCoreDbg, development fully based on GitHub to be more collaborative and flexible.

**Project goals:** implement close to MSVS C# debugger functionality (excluding related to MSVS IDE and not DAP supported features), complete [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) support, low memory consumption and high performance.

## Development process

The project's development strategy is Trunk-Based Development, this means you can build current upstream code and be sure you have last version with all features and fixes included. You can find upcoming changes in [CHANGELOG.md](CHANGELOG.md).

## Building from Source Code

- [Windows OS build.](docs/build_windows.md)
- [Linux OS build.](docs/build_linux.md)
- [Local testing.](test-suite/README.md)

## Usage

- [VSCode IDE, Windows OS.](docs/usage_vscode_windows.md)
- [VSCode IDE, Linux OS.](docs/usage_vscode_linux.md)
