# Inputting text into the target process

## Inputting text from Debug Console

Add `"console": "internalConsole"` line into `.vscode/launch.json` file configuration.

When using `internalConsole`, you can input text into VSCode that will be returned from `Console.ReadLine` and similar APIs that read from `stdin`. To do so, while the program is running, type text into the input box at the bottom of the Debug Console. Pressing `Enter` will send the text to the target process. Note that if you enter text in this box while your program is stopped under the debugger, this text will be evaluated as a C# expression, not sent to the target process.

More info: https://aka.ms/VSCode-CS-LaunchJson-Console
