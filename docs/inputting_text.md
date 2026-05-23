# Inputting text into the target process

***Note: parameters below work only in case the target app is launched into the debugger and will not work if you are attaching the debugger to an already running process.***

</br>

## Inputting text from Debug Console (Evaluate Request)

Can be enabled by `DNCDBG_CONSOLE` variable with the `internalConsole` value in `env` dictionary in the [Launch Request](dap_status.md#launchrequest-launch), or (as a fallback) as the `console` field with the `internalConsole` value in the [Launch Request](dap_status.md#launchrequest-launch).

**VSCode IDE:** Add the `"console": "internalConsole"` property to your `.vscode/launch.json` configuration file. VSCode will then include the `console` field with the `internalConsole` value in the [Launch Request](dap_status.md#launchrequest-launch).

When using `internalConsole`, you can input text that will be returned from `Console.ReadLine` and similar APIs that read from `stdin` by sending an [Evaluate Request](dap_status.md#evaluaterequest-evaluate) with the text you need. Note that if you send an Evaluate Request with text while your program is stopped under the debugger, this text will be evaluated as a C# expression, not sent to the target process.

To do so in VSCode, while the program is running, type text into the input box at the bottom of the Debug Console in VSCode IDE. Pressing `Enter` will send the text to the target process.

More info: https://aka.ms/VSCode-CS-LaunchJson-Console

</br>

## Remote Console

Can be enabled by the `DNCDBG_CONSOLE` variable with the `remoteConsole` value in `env` dictionary in the [Launch Request](dap_status.md#launchrequest-launch), or (as a fallback) as the `console` field with the `remoteConsole` value in the [Launch Request](dap_status.md#launchrequest-launch).

When using `remoteConsole`, the debugger will start a local server and open a TCP port for connection. Users can use an external program (for example, netcat - `nc localhost 22534`) as an external terminal. When using this mode, you will need to switch focus between the IDE and the external program. All input from the external program will be routed to the target process stdin, and all target process stdout and stderr streams will be routed to the external program as well.

The default TCP port used is `22534`. This can be changed by providing the `DNCDBG_REMOTECONSOLEPORT` variable with the number as **string** value in `env` dictionary in the [Launch Request](dap_status.md#launchrequest-launch).

***Note: VSCode IDE doesn't support the `remoteConsole` value for the `console` property in the `.vscode/launch.json` configuration file, use `env` instead, for example:***
```json
        {
            ...
            "env": {
                "DNCDBG_CONSOLE" : "remoteConsole",
                "DNCDBG_REMOTECONSOLEPORT" : "22534"
            },
            ...
        }
```

</br>

## External Terminal (Windows OS only)

Can be enabled by the `DNCDBG_CONSOLE` variable with the `externalTerminal` value in `env` dictionary in the [Launch Request](dap_status.md#launchrequest-launch), or (as a fallback) as the `console` field with the `externalTerminal` value in the [Launch Request](dap_status.md#launchrequest-launch).

**VSCode IDE:** Add the `"console": "externalTerminal"` property to your `.vscode/launch.json` configuration file. VSCode will then include the `console` field with the `externalTerminal` value in the [Launch Request](dap_status.md#launchrequest-launch).

When using `externalTerminal`, the target process will run inside its own external terminal. You will need to switch focus between VSCode and the external terminal window.

***Note:***
 - ***This is a Windows OS only feature implemented inside Windows API (`CreateProcessW` with `CREATE_NEW_CONSOLE` creation flag). This feature is not implemented in dbgshim for other OSes.***
 - ***All stdout and stderr are routed directly to the external terminal; the debugger will not emit stdout and stderr output events from the debuggee process.***
