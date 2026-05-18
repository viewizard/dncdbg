# Inputting text into the target process

***Note: parameters below work only in case the target app is launched into the debugger and will not work if you are attaching the debugger to an already running process.***

## Inputting text from Debug Console

Add the `"console": "internalConsole"` property to your `.vscode/launch.json` configuration file.
VSCode will then include the `console` field with the `internalConsole` value in the [Launch Request](dap_status.md#launchrequest-launch).

When using `internalConsole`, you can input text into VSCode that will be returned from `Console.ReadLine` and similar APIs that read from `stdin`. To do so, while the program is running, type text into the input box at the bottom of the Debug Console. Pressing `Enter` will send the text to the target process. Note that if you enter text in this box while your program is stopped under the debugger, this text will be evaluated as a C# expression, not sent to the target process.

More info: https://aka.ms/VSCode-CS-LaunchJson-Console
