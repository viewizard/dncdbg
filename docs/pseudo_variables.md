## Debugger Pseudo-Variables

**$exception** : Gets an object that represents an exception that is currently being thrown on a thread by managed code. This is particularly useful for inspecting the details of an exception even if it wasn't caught in a `try-catch` block.
Note: The exception object will exist from the time the exception is thrown until the end of the catch block.

**$pid** : Gets the process ID. This is particularly useful for identifying the debugged process or for breakpoint conditions.

**$tid** : Gets the thread ID for the current thread. This is particularly useful for breakpoint condition setup in case you need to investigate a particular thread's work.
