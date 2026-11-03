## Logpoints

Related to [Breakpoint - Logpoints](https://code.visualstudio.com/docs/csharp/debugging#_breakpoint---logpoints)

Logpoints (also known as tracepoints in Visual Studio) let you send output to the Debug Console without editing code. Unlike breakpoints, they do not stop the execution flow of your application.

Any expression enclosed in curly braces ('{' and '}') is evaluated when the logpoint is hit.

The following special tokens can also be used in these expressions:

| Token | Description | Example Output |
| ----- | ----------- | -------------- |
| **$FILEPOS** | Current source file path and line number | "/home/user/Program.cs:12" |
| **$FUNCTION** | Current method name, including the declaring type and parameters | "TestLogpoints.Program.Main(string[] args)" |
| **$ADDRESS** | Current instruction address | "0x00007ff107a81915" |
| **$TID** | Thread ID | 43655 |
| **$PID** | Process ID | 13643 |
| **$TNAME** | Current thread name | "Main Thread" |
| **$HITCOUNT** | Number of times this breakpoint has been hit | 2 |

Note that string results are displayed in quotes with character escaping. You can also use [format specifiers](evaluation_format_specifiers.md), for example, `{$FILEPOS,nq}` displays the string without quotes.
