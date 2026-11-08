# Multi-session Persistent Debug Server

DNCDbg supports the multi-session persistent debug server mode. This means you can run several debug sessions one after another without restarting the debugger for each session.

## Example workflow

- Start the debugger.

**First debug session**

- Start the initialization sequence with the [Initialize Request](dap_status.md#initializerequest-initialize).
- ... perform the initialization you need ...
- Finish the initialization sequence with the [ConfigurationDone Request](dap_status.md#configurationdonerequest-configurationdone).
- Start the debug session with the [Launch Request](dap_status.md#launchrequest-launch) or the [Attach Request](dap_status.md#attachrequest-attach).
- ... debug the application ...
- Restart the debug session with the [Restart Request](dap_status.md#restartrequest-restart).
- ... debug the application ...
- Finish the debug session with the [Terminate Request](dap_status.md#terminaterequest-terminate) or the [Detach Request](dap_status.md#detachrequest-detach).
  ***Note: the [Detach Request](dap_status.md#detachrequest-detach) is not part of the DAP specification.***

**Second debug session**

- Start the initialization sequence with the [Initialize Request](dap_status.md#initializerequest-initialize).
- ...
- Finish the debug session with the [Terminate Request](dap_status.md#terminaterequest-terminate) or the [Detach Request](dap_status.md#detachrequest-detach).

**Last debug session**

- Start the initialization sequence with the [Initialize Request](dap_status.md#initializerequest-initialize).
- ...
- Finish the debug session and close the debugger with the [Disconnect Request](dap_status.md#disconnectrequest-disconnect).

**OR**

- Start the initialization sequence with the [Initialize Request](dap_status.md#initializerequest-initialize).
- ...
- Finish the debug session with the [Terminate Request](dap_status.md#terminaterequest-terminate) or the [Detach Request](dap_status.md#detachrequest-detach).
- ...
- Close the debugger with the [Disconnect Request](dap_status.md#disconnectrequest-disconnect).

## Protocol server mode (`--server`)

By default, DNCDbg exchanges DAP messages with the client over the standard input and output streams. The `--server` command-line option changes this: the debugger listens on a loopback TCP port, and the DAP messages are exchanged with the client over the accepted TCP connection instead.

Usage:

- `--server` - listen on the default TCP port `4711`.
- `--server=port` - listen on the specified TCP port (range 1 - 65535), for example `--server=5000`.

Example:

```
dncdbg --server=5000
```

Behavior:

- The debugger binds only to the loopback interface (`127.0.0.1`), so the server is reachable only from the local machine.
- The debugger accepts exactly one client connection. Other connection attempts are not served.
- The debugger starts listening as soon as it is launched, but the protocol messages are read only after the client connects.
- The messages use the same format as in stdin/stdout mode (`Content-Length` headers followed by the JSON payload), so the same DAP requests and responses apply. See the [DAP status](dap_status.md) for the supported requests.
- When the client disconnects, the debugger reports the end of the input stream, initiates a forced disconnect, and shuts down.
- If the port is invalid or cannot be used (for example, it is already in use), the debugger reports an error and exits without starting a debug session.
- The `--logProtocol=` and `--log=` options can be used as usual to trace the protocol messages and the debugger activity.

***Note: this mode replaces stdin/stdout for the protocol messages, so the process's standard output is not used for DAP messages in server mode.***
