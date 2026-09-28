## Debugger configuration environment variables

These environment variables can be set in the DNCDbg process environment before the debugger is launched, or passed as part of the `env` option of the `launch`, `attach`, or `restart` request for a particular debug session configuration.

**DNCDBG_STACKTRACE_LIMIT** : maximum number of frames in a stack trace; defaults to 250

**DNCDBG_DAP_REQUEST_TIMEOUT** : timeout for executing a single DAP request, in milliseconds; defaults to 15000 ms (15 seconds)

**DNCDBG_NORMAL_EVAL_TIMEOUT** : timeout for executing an evaluation, in milliseconds; defaults to 5000 ms (5 seconds)

**DNCDBG_ABORT_EVAL_TIMEOUT** : timeout for aborting an evaluation that exceeds the evaluation timeout, in milliseconds; defaults to 5000 ms (5 seconds)

**DNCDBG_HTTP_REQUEST_TIMEOUT** : timeout for executing an HTTP/HTTPS request, in seconds; defaults to 60 seconds
