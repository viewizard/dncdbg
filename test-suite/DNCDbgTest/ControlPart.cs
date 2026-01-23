using System;

using DNCDbgTest;

namespace DNCDbgTestCore
{
public class ControlPart
{
    public void Run(ControlScript script, DebuggerClient debugger, DNCDbgTestCore.Environment env)
    {
        ControlInfo info = new ControlInfo(script, env);
        script.ExecuteCheckPoints(info, debugger);
    }
}

public class Environment
{
    public string TestName = null;
    public string SourceFilesPath = null;
    public string TargetAssemblyPath = null;
    public string CorerunPath = "dotnet";
}
}
