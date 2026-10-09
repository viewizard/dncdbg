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
