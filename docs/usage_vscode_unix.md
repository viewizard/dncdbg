# VS Code IDE, local debugging, Linux and macOS OSes

1. Install `C#` extension from Microsoft
2. Switch to `Run and Debug` panel
3. Click on `Generate C# Assets for Build and Debug` button
4. Open the created file inside your project `.vscode/launch.json`
5. Add `.NET Core Launch with DNCDbg` configuration, for example:

```json
        {
            "name": ".NET Core Launch with DNCDbg",
            "type": "coreclr",
            "request": "launch",
            "preLaunchTask": "build",
            "env": {
                // "ENV_NAME" : "value"
            },
            "sourceFileMap": {
                // Maps any path returned by the debugger
                // that begins with "C:\\test1\\test3\\Project.cs" to "/test1/test2/Project.cs":
                // "C:\\test1\\test3\\Project.cs": "/test1/test2/Project.cs",
                // Maps any path returned by the debugger that begins with "C:\\Dir1" to "/dir1":
                // "C:\\Dir1": "/dir1"
            },
            "program": "${workspaceFolder}/bin/Debug/net10.0/your_app.dll",
            "args": [],
            "cwd": "${workspaceFolder}",
            "console": "internalConsole",
            "stopAtEntry": false,
            "justMyCode" : true,
            "enableStepFiltering": true,
            // Note: dncdbg behaves differently from VS Code's VsDbg:
            // if the DLL has debug symbols, the debugger suppresses JIT optimizations.
            "suppressJITOptimizations": false,
            "expressionEvaluationOptions": {
                "allowImplicitFuncEval": true,
                "allowToString": true,
                "showRawValues": false
            },
            "logging": {
                "diagnosticsLog" : {
                    "ProtocolMessages": true
                }
            },
            "pipeTransport": {
                "pipeCwd": "${workspaceFolder}",
                "pipeProgram": "/bin/bash",
                "pipeArgs": ["-c"],
                "debuggerPath": "/path/to/dncdbg/bin/dncdbg"
            }
        }
```
