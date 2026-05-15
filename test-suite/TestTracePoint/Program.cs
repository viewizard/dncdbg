using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestTracePoint
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "trace_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test1", null, null, "Start test.");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test2", null, null, "x={x}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test3", null, null, "x={x}, y={y}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test4", null, null, "x==>>{x*5}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test5", null, null, "y={y}y}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test6", null, null, "z={z}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "trace_test7", null, null, "y={y{y}");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("Start");

        int x = 1;                                                   Label.Breakpoint("trace_test1");
        int y = 2;                                                   Label.Breakpoint("trace_test2");
        x++;                                                         Label.Breakpoint("trace_test3");
        y++;                                                         Label.Breakpoint("trace_test4");
        x++;                                                         Label.Breakpoint("trace_test5");
        x++;                                                         Label.Breakpoint("trace_test6");
        x++;                                                         Label.Breakpoint("trace_test7");

        Console.WriteLine("End");                                    Label.Breakpoint("bp1");

        Label.Checkpoint("trace_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");

                Context.WasOutputEvent("console", "Start test.\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "x=1\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "x=1, y=2\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "x==>>10\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "y=3y}\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "z={error: The name 'z' does not exist in the current context}\n", @"__FILE__:__LINE__");
                Context.WasOutputEvent("console", "y={y{y}\n", @"__FILE__:__LINE__");

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
