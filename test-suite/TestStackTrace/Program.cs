using System;
using System.Threading.Tasks;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestStackTrace
{
class Program
{
    static void Abc()
    {
        ;                                                                 Label.Breakpoint("normal_trace1");
    }

    static void Bcd()
    {
        try
        {
            Cde(5, 2);
        }
        catch (InvalidOperationException error)
        {
            Cde();                                                        Label.Breakpoint("catch_trace2");
        }
    }

    static void Cde()
    {
        ;                                                                 Label.Breakpoint("catch_trace1");
    }

    static int Cde(int a, int b)
    {
        throw new InvalidOperationException("boom");                      Label.Breakpoint("unhandled_async_trace1");
        return a+b;
    }

    static async Task Main(string[] args)
    {
        Label.Checkpoint("init", "normal_trace",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "normal_trace1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "catch_trace1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        await Task.Yield();
        Abc();                                                            Label.Breakpoint("normal_trace2");

        Label.Checkpoint("normal_trace", "catch_trace",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "normal_trace1");

                string[] frameNames = [ "TestStackTrace.Program.Abc()",
                                        "TestStackTrace.Program.Main(string[] args)" ];
                string[] frameLocations = [ "normal_trace1",
                                            "normal_trace2" ];
                Context.TestStackTrace(@"__FILE__:__LINE__", frameNames, frameLocations);

                Context.Continue(@"__FILE__:__LINE__");
            });

        Bcd();                                                            Label.Breakpoint("catch_trace3");

        Label.Checkpoint("catch_trace", "unhandled_async_trace",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "catch_trace1");

                string[] frameNames = [ "TestStackTrace.Program.Cde()",
                                        "TestStackTrace.Program.Bcd()",
                                        "TestStackTrace.Program.Main(string[] args)" ];
                string[] frameLocations = [ "catch_trace1",
                                            "catch_trace2",
                                            "catch_trace3" ];
                Context.TestStackTrace(@"__FILE__:__LINE__", frameNames, frameLocations);

                Context.Continue(@"__FILE__:__LINE__");
            });

        Cde(1, 2);                                                        Label.Breakpoint("unhandled_async_trace2");

        Label.Checkpoint("unhandled_async_trace", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "unhandled_async_trace1", "CLR", "unhandled", "System.InvalidOperationException");

                string[] frameNames = [ "[Exception] TestStackTrace.Program.Cde(int a, int b)",
                                        "[Exception] TestStackTrace.Program.Main(string[] args)" ];
                string[] frameLocations = [ "unhandled_async_trace1",
                                            "unhandled_async_trace2" ];
                Context.TestStackTrace(@"__FILE__:__LINE__", frameNames, frameLocations);
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                // At this point the debugger stops at the unhandled exception; there is no reason to continue the process, abort execution.
                Context.AbortExecution(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__", checkExitCode: null);
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
