using System;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestStdIO
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testoutput",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp5");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp6");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp7");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

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

        Console.Error.WriteLine("test \001\002 forbidden \036\037 chars");

        i++;                                                      Label.Breakpoint("bp5");

        Debugger.Log(0, "Info", "Application started.");

        i++;                                                      Label.Breakpoint("bp6");

        Label.Checkpoint("testoutput", "testinput",
            (Object context) =>
            {
                string endLine = "\n";
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    endLine = "\r\n";

                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stderr", "test stdout" + endLine);
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stdout", "test stderr" + endLine);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "test stdout" + endLine);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "test more stdout" + endLine);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stderr", "test");
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stderr", "stderr" + endLine);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test stderr" + endLine);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp4");
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test more stderr" + endLine);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test \u000001\u000002 forbidden \u000036\u000037 chars" + endLine);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp6");
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "Application started.\n");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("test stdin");

        string? s = Console.ReadLine();
        Console.WriteLine("input text: " + s);

        i++;                                                      Label.Breakpoint("bp7");

        Label.Checkpoint("testinput", "finish",
            (Object context) =>
            {
                System.Threading.Thread.Sleep(3000);

                Context Context = (Context)context;
                Context.CalcExpressionWithStatusOnlyCheck(@"__FILE__:__LINE__", 0, "new added text", true);

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp7");

                string endLine = "\n";
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    endLine = "\r\n";

                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "input text: new added text" + endLine);
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
