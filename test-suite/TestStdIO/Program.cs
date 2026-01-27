using System;
using System.IO;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestStdIO
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testio",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("test stdout");
        int i = 1;                                                Label.Breakpoint("bp1");

        Console.WriteLine("test more stdout");

        i++;                                                      Label.Breakpoint("bp2");

        Console.Error.WriteLine("test stderr");

        i++;                                                      Label.Breakpoint("bp3");

        Console.Error.WriteLine("test more stderr");

        i++;                                                      Label.Breakpoint("bp4");

        Label.Checkpoint("testio", "finish",
            (Object context) =>
            {
                string endLine = "\n";
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    endLine = "\r\n";

                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.FailedOutputEventCheck("stderr", "test stdout" + endLine, @"__FILE__:__LINE__");
                Context.FailedOutputEventCheck("stdout", "test stderr" + endLine, @"__FILE__:__LINE__");
                Context.WasOutputEvent("stdout", "test stdout" + endLine, @"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
                Context.WasOutputEvent("stdout", "test more stdout" + endLine, @"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                Context.FailedOutputEventCheck("stderr", "test", @"__FILE__:__LINE__");
                Context.FailedOutputEventCheck("stderr", "stderr" + endLine, @"__FILE__:__LINE__");
                Context.WasOutputEvent("stderr", "test stderr" + endLine, @"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp4");
                Context.WasOutputEvent("stderr", "test more stderr" + endLine, @"__FILE__:__LINE__");
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
