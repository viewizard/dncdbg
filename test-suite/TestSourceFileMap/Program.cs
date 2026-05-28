using System;
using System.IO;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestSourceFileMap
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testsourcemap",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");

                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                string path = Path.GetDirectoryName(Context.GetSourceFilesPath());
                if (isWindows)
                {
                    Context.AddSourceFileMapEntry(path, "/test/folder");
                }
                else
                {
                    Context.AddSourceFileMapEntry(path, "C:\\test\\folder");
                }

                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                if (isWindows)
                {
                    Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp1", "/test/folder/Program.cs");
                }
                else
                {
                    Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp1", "C:\\test\\folder\\Program.cs");
                }
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testsourcemap", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1", checkSourcePath: false);
                Context.Continue(@"__FILE__:__LINE__");
            });

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
