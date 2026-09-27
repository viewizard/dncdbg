## Debugger configuration environment variables

These environment variables can be set in the DNCDbg process environment before the debugger is launched, or passed as part of the `env` option of the `launch` or `restart` request for a particular debug session configuration.

**DNCDBG_STACKTRACE_LIMIT** : maximum number of frames in a stack trace; defaults to 250

**DNCDBG_DAP_REQUEST_TIMEOUT** : timeout for executing a single DAP request, in milliseconds; defaults to 15000 ms (15 seconds)
