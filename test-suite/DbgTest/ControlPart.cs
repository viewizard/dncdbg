using System;

using DbgTest;

namespace DbgTestCore
{
public class ControlPart
{
    public static void Run(ControlScript script, DebuggerClient debugger, DbgTestCore.Environment env)
    {
        script.ExecuteCheckPoints(new ControlInfo(script, env), debugger);
    }
}

public class Environment
{
    public string? TestName { get; set; }
    public string? SourceFilesPath { get; set; }
    public string? TargetAssemblyPath { get; set; }
    public string CorerunPath { get; set; } = "dotnet";
}
}
