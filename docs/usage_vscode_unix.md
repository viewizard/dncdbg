# VSCode IDE, local debugging, Linux and macOS OSes

1. Install `C#` extension from Microsoft
2. Switch to `Run and Debug` panel
3. Click on `Generate C# Assets for Build and Debug` button
4. Open the created file inside your project `.vscode/launch.json`
5. Add `.NET Core Launch with DNCDbg` configuration:

```json
        {
            "name": ".NET Core Launch with DNCDbg",
            "type": "coreclr",
            "request": "launch",
            "preLaunchTask": "build",
            "env": {
                // "ENV_NAME" : "value"
            },
            "program": "${workspaceFolder}/bin/Debug/net10.0/vscode_test.dll",
            "args": [],
            "cwd": "${workspaceFolder}",
            "console": "internalConsole",
            "stopAtEntry": false,
            "justMyCode" : true,
            "enableStepFiltering": true,
            // Note: dncdbg has different behaviour compared to VSCode vsdbg, in case dll has debug symbols debugger suppresses JIT optimization.
            "suppressJITOptimizations": false,
            "expressionEvaluationOptions": {
                "allowImplicitFuncEval": true
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
