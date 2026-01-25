using System;

using DbgTest;

namespace DbgTestCore
{
public class ControlPart
{
    public void Run(ControlScript script, DebuggerClient debugger, DbgTestCore.Environment env)
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
