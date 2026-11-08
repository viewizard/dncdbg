# Launch Options

DNCDbg accepts command-line options that control how the debugger runs: the transport used to exchange [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) messages, logging, version and build information.

```
dncdbg [options]
```

Options can be specified in any order. An unknown option makes the debugger print an error and exit.

## Options

| Option | Description |
| --- | --- |
| `--help` | Print the usage message and exit. |
| `--version` | Print the current version and exit. |
| `--buildinfo` | Print the build info and exit. |
| `--server[=port]` | Serve DAP messages over a loopback TCP connection instead of stdin/stdout. |
| `--log=<path to log file>` | Enable debugger logging to the specified file. |
| `--logProtocol=<path to log file>` | Enable protocol interaction logging to the specified file. |
| `--loglevel=number/level` | Set the minimal logging level. |
| `--interpreter=vscode` | Internal option (added silently by the VS Code IDE); see [internal options](#internal-options). |

## Protocol server mode (`--server`)

By default, DNCDbg exchanges DAP messages with the client over the standard input and output streams. The `--server` option changes this: the debugger listens on a loopback TCP port, and the DAP messages are exchanged with the client over the accepted TCP connection instead.

Usage:

- `--server` - listen on the default TCP port `4711`.
- `--server=port` - listen on the specified TCP port (range 1 - 65535), for example `--server=5000`.

```
dncdbg --server=5000
```

For a full description of this mode (the example workflow and the behavior), see [Multi-session Persistent Debug Server](debug_server.md).

## Logging

The logging options control where the debugger writes its diagnostic output:

- `--log=<path to log file>` - write the debugger log to the specified file. The file is opened in append mode.
- `--logProtocol=<path to log file>` - write all DAP protocol messages (both incoming and outgoing) to the specified file. Useful for inspecting the interaction between the debugger and the client.
- `--loglevel=number/level` - set the minimal level of the messages written to the log. The level can be specified as a number or a case-insensitive name:

| Value | Level | Messages |
| --- | --- | --- |
| `0` or `DEBUG` | Debug | debug messages (debug builds only) |
| `1` or `INFO` | Info | informational messages |
| `2` or `WARNING` | Warning | warnings |
| `3` or `ERROR` | Error | errors |

By default, the level is set to `INFO`. If the specified level is unknown, the debugger writes a warning to the log and uses the `INFO` level.

***Note: the DEBUG level (`0` or `DEBUG`) is available for debug builds only.***

Example:

```
dncdbg --log=/tmp/dncdbg.log --logProtocol=/tmp/dncdbg_protocol.log --loglevel=WARNING
```

## Version and build info

The `--version` option prints the current version:

```
DNCDbg version 1.3.0
```

The `--buildinfo` option prints the version along with the build details: build type, build date, target OS and architecture, compiled options, VCS info, and the copyright notice:

```
DNCDbg version 1.3.0

Build info:
      Build type:  Debug
      Build date:  Nov  8 2026 12:35:14
      Target OS:   Darwin
      Target arch: arm64

Compiled options:
      CASE_INSENSITIVE_FILENAME_COLLISION

DNCDbg VCS info:   f15747a

Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
Copyright (c) 2026 Mikhail Kurinnoi
Distributed under the MIT License.
```

## Help

The `--help` option prints the usage message:

```
Usage: dncdbg [options]

Options:
--buildinfo                              Print build info.
--logProtocol=<path to log file>         Enable protocol interaction logging to file.
--log=<path to log file>                 Enable debugger logging to file.
--loglevel=number/level                  Minimal logging level:
                                         0 or DEBUG (available for debug build only)
                                         1 or INFO
                                         2 or WARNING
                                         3 or ERROR
                                         by default, set to INFO.
--server[=port]                          Starts the debugger listening for requests on the
                                         specified TCP/IP port instead of stdin/stdout. If the
                                         port is not specified, the default TCP port 4711 is used.
--version                                Displays the current version.
```

## Internal options

- `--interpreter=vscode` - the VS Code IDE adds this option silently when it launches the debugger, so there is no need to specify it manually. It tells the debugger that it runs under the VS Code IDE. VS Code does not support restarting a debug session (only restarting the entire debugger), so the debugger does not advertise the `supportsRestartRequest` capability in this mode.
