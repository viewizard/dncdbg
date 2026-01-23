using System;
using System.IO;
using System.Net;
using System.Collections.Generic;
using System.Diagnostics;

using LocalDebugger;
using DNCDbgTestCore;
using DNCDbgTestCore.DAP;

namespace Runner
{
class Program
{
    static int Main(string[] args)
    {
        var cli = new CLInterface(args);
        DebuggerClient debugger = null;
        ControlScript script = null;
        LocalDebuggerProcess localDebugger = null;

        if (cli.NeedHelp)
        {
            cli.PrintHelp();
            return 1;
        }

        if (cli.ClientInfo == null)
        {
            Console.Error.WriteLine("Please define client type");
            return 1;
        }

        try
        {
            var localClientInfo = (LocalClientInfo)cli.ClientInfo;
            localDebugger = new LocalDebuggerProcess(localClientInfo.DebuggerPath, @"");
            localDebugger.Start();

            debugger = new DAPLocalDebuggerClient(localDebugger.Input, localDebugger.Output);
        }
        catch
        {
            Console.Error.WriteLine("Can't create debugger client");
            if (localDebugger != null)
            {
                localDebugger.Close();
            }
            return 1;
        }

        if (!debugger.DoHandshake(5000))
        {
            Console.Error.WriteLine("Handshake is failed");
            debugger.Close();
            if (localDebugger != null)
            {
                localDebugger.Close();
            }
            return 1;
        }

        try
        {
            script = new ControlScript(cli.Environment.SourceFilesPath, debugger.Protocol);
        }
        catch (ScriptNotBuiltException e)
        {
            Console.Error.WriteLine("Script is not built:");
            Console.Error.WriteLine(e.ToString());
            debugger.Close();
            if (localDebugger != null)
            {
                localDebugger.Close();
            }
            return 1;
        }

        try
        {
            new ControlPart().Run(script, debugger, cli.Environment);
            Console.WriteLine("Success: Test case \"{0}\" is passed!!!", cli.Environment.TestName);
        }
        catch (System.Exception e)
        {
            Console.Error.WriteLine("Script running is failed. Got exception:\n" + e.ToString());
            debugger.Close();
            if (localDebugger != null)
            {
                localDebugger.Close();
            }
            return 1;
        }

        debugger.Close();
        if (localDebugger != null)
        {
            localDebugger.Close();
        }
        return 0;
    }
}

class CLInterface
{
    public CLInterface(string[] args)
    {
        Environment = new DNCDbgTestCore.Environment();

        if (args.Length == 0)
        {
            NeedHelp = true;
            return;
        }

        if (args.Length == 1 && (args[0] == "-h" || args[0] == "--help"))
        {
            NeedHelp = true;
            return;
        }

        int i = 0;
        while (i < args.Length && !NeedHelp)
        {
            switch (args[i])
            {
            case "--local":
                if (i + 1 >= args.Length)
                {
                    NeedHelp = true;
                    break;
                }

                try
                {
                    string debuggerPath = Path.GetFullPath(args[i + 1]);
                    ClientInfo = new LocalClientInfo(debuggerPath);
                }
                catch
                {
                    NeedHelp = true;
                    break;
                }
                i += 2;

                break;
            case "--test":
                if (i + 2 >= args.Length)
                {
                    NeedHelp = true;
                    break;
                }

                try
                {
                    Environment.TestName = args[i + 1];
                }
                catch
                {
                    NeedHelp = true;
                    break;
                }

                i += 2;

                break;
            case "--sources":
                if (i + 1 >= args.Length)
                {
                    NeedHelp = true;
                    break;
                }

                try
                {
                    Environment.SourceFilesPath = Path.GetFullPath(args[i + 1]);
                    if (Environment.SourceFilesPath[Environment.SourceFilesPath.Length - 1] == ';')
                    {
                        Environment.SourceFilesPath =
                            Environment.SourceFilesPath.Remove(Environment.SourceFilesPath.Length - 1);
                    }
                }
                catch
                {
                    NeedHelp = true;
                    break;
                }

                i += 2;

                break;
            case "--assembly":
                if (i + 1 >= args.Length)
                {
                    NeedHelp = true;
                    break;
                }

                Environment.TargetAssemblyPath = args[i + 1];

                i += 2;

                break;
            case "--dotnet":
                if (i + 1 >= args.Length)
                {
                    NeedHelp = true;
                }

                try
                {
                    Path.GetFullPath(args[i + 1]);
                }
                catch
                {
                    NeedHelp = true;
                    break;
                }

                Environment.CorerunPath = args[i + 1];

                i += 2;

                break;
            default:
                NeedHelp = true;
                break;
            }
        }

        if (ClientInfo != null && !File.Exists(Environment.TargetAssemblyPath))
        {
            Console.Error.WriteLine("Provided assembly path is invalid");
            throw new System.Exception();
        }

        if (NeedHelp)
        {
            return;
        }
    }

    public void PrintHelp()
    {
        Console.Error.WriteLine(@"usage: dotnet run {-h|--help|[OPTIONS] TESTS}
options:
    --dotnet dotnet-path    Set dotnet path(default: dotnet-path=""dotnet"")
    --local debugger-path   Create launch debugger locally and create client
    --test name             Test name
    --sources path[;path]   Semicolon separated paths to source files
    --assembly path         Path to target assambly file
    
    
    ");
    }

    public bool NeedHelp = false;
    public ProtocolType Protocol = ProtocolType.DAP;
    public DNCDbgTestCore.Environment Environment;
    public LocalClientInfo ClientInfo;
}

class LocalClientInfo
{
    public LocalClientInfo(string debuggerPath)
    {
        DebuggerPath = debuggerPath;
    }

    public string DebuggerPath;
}
}
