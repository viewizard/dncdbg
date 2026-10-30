# Launch Settings (launchSettings.json)

DNCDbg can take the launch configuration from a `launchSettings.json` file — the same file used by Visual Studio and `dotnet run`. This allows the launch settings (command-line arguments, environment variables, working directory, executable) to be shared between IDEs and tools.

The file is applied through two options of the [Launch Request](dap_status.md#launchrequest-launch) (these options are not part of the DAP specification):

- `launchSettingsFilePath` — the path to the `launchSettings.json` file. By default, the debugger searches for the file in `{cwd}/Properties/launchSettings.json`, where `cwd` is the `cwd` option of the [Launch Request](dap_status.md#launchrequest-launch). A relative path is resolved against `cwd` (or against the debugger's current working directory, when `cwd` is not set).
- `launchSettingsProfile` — the name of the profile to use from the file:
    - when the option is not provided, the first profile with `"commandName": "Project"` is used;
    - when the option is set to `null` or an empty string, the `launchSettings.json` file is ignored entirely;
    - otherwise, the profile with the given name is used.

***Note: the `launchSettings.json` file may contain comments; they are ignored during parsing.***

## Profile properties

Only the following profile properties are applied. For every property, the value from the [Launch Request](dap_status.md#launchrequest-launch) takes precedence over the value from the profile.

| Property | Applied as | Used when |
| --- | --- | --- |
| `commandName` | Selects how the target is launched. Only the `Project` and `Executable` command names are supported; both are launched the same way: the target path is the `program` option from the [Launch Request](dap_status.md#launchrequest-launch), or the `executablePath` profile property when the request provides no `program` (the target is wrapped with `dotnet` when the path ends with `.dll`). | always |
| `executablePath` | The `program` option. A relative path is resolved against the directory containing the `launchSettings.json` file. | the [Launch Request](dap_status.md#launchrequest-launch) provides no `program` |
| `workingDirectory` | The `cwd` option. A relative path is resolved against the directory containing the `launchSettings.json` file. | the [Launch Request](dap_status.md#launchrequest-launch) provides no `cwd` |
| `commandLineArgs` | The `args` option. The string is split into separate arguments: whitespace separates them unless it is inside double quotes; a backslash followed by a double quote escapes the quote. | the [Launch Request](dap_status.md#launchrequest-launch) provides no `args` (or an empty one) |
| `environmentVariables` | The `env` option. The variables are merged into the debuggee process environment; variables already set by the [Launch Request](dap_status.md#launchrequest-launch) are not overridden. | always (per variable) |

All other profile properties (e.g. `launchBrowser`, `applicationUrl`, `sslPort` and `httpPort`) are ignored.

## Errors

- When neither `launchSettingsProfile` nor `launchSettingsFilePath` is provided (the default search location is used), a missing file, a file that cannot be parsed or a file without a suitable profile does not fail the launch — the debugger simply launches the target without the launch settings.
- When `launchSettingsProfile` (a profile name) or `launchSettingsFilePath` is provided explicitly, problems are reported as launch errors: the file can't be opened or parsed, no suitable profile is found in the file, the named profile is missing, or the named profile has an unsupported `commandName`.

## Examples

Launch the project with the profile from the default search location:

`launchSettings.json`:
```json
{
    "profiles": {
        "MyProject": {
            "commandName": "Project",
            "commandLineArgs": "argument1 \"argument with spaces\"",
            "workingDirectory": "/home/user/projects/MyProject",
            "environmentVariables": {
                "MY_VARIABLE": "my value"
            }
        }
    }
}
```

`launch.json`:
```json
{
    "configurations": [
        {
            "name": ".NET Core Launch with DNCDbg",
            "type": "coreclr",
            "request": "launch",
            "preLaunchTask": "build",
            "program": "${workspaceFolder}/bin/Debug/net8.0/MyProject.dll",
            "cwd": "${workspaceFolder}",
            "launchSettingsProfile": "MyProject"
        }
    ]
}
```

Launch an executable using the explicitly selected profile:

`launchSettings.json`:
```json
{
    "profiles": {
        "MyTool": {
            "commandName": "Executable",
            "executablePath": "../tools/MyTool",
            "commandLineArgs": "--input data.txt",
            "environmentVariables": {
                "MY_VARIABLE": "my value"
            }
        }
    }
}
```

`launch.json`:
```json
{
    "configurations": [
        {
            "name": ".NET Core Launch with DNCDbg",
            "type": "coreclr",
            "request": "launch",
            "cwd": "${workspaceFolder}",
            "launchSettingsProfile": "MyTool"
        }
    ]
}
```

***Note: in the example above, the relative `executablePath` is resolved against the directory containing the `launchSettings.json` file (`${workspaceFolder}/Properties/` by default), so `../tools/MyTool` points to `${workspaceFolder}/tools/MyTool`.***

***Note: the [Restart Request](dap_status.md#restartrequest-restart) reuses the (possibly updated) launch arguments, so the launch settings are also applied on restart.***
