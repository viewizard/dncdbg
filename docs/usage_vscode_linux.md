# VSCode IDE, local debugging, Linux OS

1. Install `C#` extention from Microsoft
2. Switch to `Run and Debug` panel
3. Click on `Generate C# Assets for Build and Debug` button
5. Open created file inside your project `.vscode/launch.json`
6. Add `.NET Core Launch with DNCDbg` configuration:

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
            "enableStepFiltering": false,
            "expressionEvaluationOptions": {
                "allowImplicitFuncEval": true
            },
            "logging": {
                "diagnosticsLog" : {
                    "ProtocolMessages": true
                },
            },
            "pipeTransport": {
                "pipeCwd": "${workspaceFolder}",
                "pipeProgram": "/usr/bin/bash",
                "pipeArgs": ["-c"],
                "debuggerPath": "/home/user/dncdbg/bin/dncdbg"
            }
        }
```
