using System;
using System.IO;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestArgs
{
class Program
{
    static int Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "test_args",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");

                Context.AddArgsListEntry("bin\\Debug\\test.dll");
                Context.AddArgsListEntry("D:\\path\\to\\dir\\");
                Context.AddArgsListEntry("D:\\path\\t o\\dir\\");
                Context.AddArgsListEntry("bin\\Debug\\");
                Context.AddArgsListEntry("bin\\Debug Folder\\test.dll");

                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);

        if (isWindows)
        {
            if (args[0] != """bin\Debug\test.dll""" ||
                args[1] != """D:\path\to\dir\""" ||
                args[2] != """D:\path\t o\dir\""" ||
                args[3] != """bin\Debug\""" ||
                args[4] != """bin\Debug Folder\test.dll""")
            {
                return 1;
            }
        }
        else
        {
            if (args[0] != """bin\Debug\test.dll""" ||
                args[1] != """D:\path\to\dir\""" ||
                args[2] != """bin\Debug\""" ||
                args[3] != """bin\Debug Folder\test.dll""")
            {
                return 1;
            }
        }

        ;                                                                 Label.Breakpoint("BREAK1");

        Label.Checkpoint("test_args", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);

                if (isWindows)
                {
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug\\\\test.dll\"", "string", "args[0]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"D:\\\\path\\\\to\\\\dir\\\\\"", "string", "args[1]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"D:\\\\path\\\\t o\\\\dir\\\\\"", "string", "args[2]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug\\\\\"", "string", "args[3]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug Folder\\\\test.dll\"", "string", "args[4]");
                }
                else
                {
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug\\\\test.dll\"", "string", "args[0]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"D:\\\\path\\\\to\\\\dir\\\\\"", "string", "args[1]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug\\\\\"", "string", "args[2]");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"bin\\\\Debug Folder\\\\test.dll\"", "string", "args[3]");
                }

                Context.Continue(@"__FILE__:__LINE__");
            });

        return 0;

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
