## Debugger configuration environment variables

These environment variables can be set in the DNCDbg process environment before the debugger is launched, or passed as part of the `env` option of the `launch`, `attach`, or `restart` request for a particular debug session configuration.

### Debug session lifecycle

| Variable | Description |
| --- | --- |
| `DNCDBG_STARTUP_TIMEOUT` | The timeout for executing a process launch or attach, in milliseconds (range 0 - 4294967295); defaults to 5000 ms (5 seconds) |
| `DNCDBG_TERMINATION_TIMEOUT` | The timeout for terminating a process, in milliseconds (range 0 - 4294967295); defaults to 3000 ms (3 seconds) |

### Timeouts

| Variable | Description |
| --- | --- |
| `DNCDBG_DAP_REQUEST_TIMEOUT` | The timeout for executing a single DAP request, in milliseconds (range 0 - 4294967295); defaults to 15000 ms (15 seconds) |
| `DNCDBG_NORMAL_EVAL_TIMEOUT` | The timeout for executing an evaluation, in milliseconds (range 0 - 4294967295); defaults to 5000 ms (5 seconds) |
| `DNCDBG_ABORT_EVAL_TIMEOUT` | The timeout for aborting an evaluation that exceeds the evaluation timeout, in milliseconds (range 0 - 4294967295); defaults to 5000 ms (5 seconds) |
| `DNCDBG_HTTP_REQUEST_TIMEOUT` | The timeout for executing an HTTP/HTTPS request, in milliseconds (range 0 - 4294967295); defaults to 60000 ms (60 seconds) |

### Limits

| Variable | Description |
| --- | --- |
| `DNCDBG_STACKTRACE_LIMIT` | The maximum number of frames in a stack trace (range 0 - 4294967295); defaults to 250 |
| `DNCDBG_MEMBERS_PER_PAGE_LIMIT` | The maximum number of members per page before a "[More]" entry is added (range 0 - 4294967295); defaults to 25 |
| `DNCDBG_ROOTHIDDEN_WALK_LIMIT` | The maximum number of members marked with DebuggerBrowsableState.RootHidden that are unwrapped in a single walk (range 0 - 4294967295); zero disables unwrapping; defaults to 32 |
| `DNCDBG_INNER_EXCEPTION_WALK_LIMIT` | The maximum number of inner exceptions traversed when building exception details (range 0 - 4294967295); zero disables the walk; defaults to 32 |

### Symbol handling

| Variable | Description |
| --- | --- |
| `DNCDBG_PDB_MMAP_POLICY` | The policy for flushing memory-mapped PDB data after module symbol initialization (range 0 - 1); zero disables flushing; defaults to 1 |
