using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestBreak
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "break_test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "break_test2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "break_test3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "break_test4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "break_test5");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test, that debugger stop at Debugger.Break() in managed code.
        Console.WriteLine("Start test.");
        Debugger.Break();                                             Label.Breakpoint("break_test1");

        Label.Checkpoint("break_test1", "break_test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakHit(@"__FILE__:__LINE__", "break_test1");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test, that debugger ignore Debugger.Break() on continue in case it already stop at breakpoint at this code line.
        Debugger.Break();                                             Label.Breakpoint("break_test2");

        Label.Checkpoint("break_test2", "break_test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "break_test2");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test, that debugger ignore Debugger.Break() on step in case it already stop at breakpoint at this code line.
        Debugger.Break();                                             Label.Breakpoint("break_test3");
        Console.WriteLine("Next Line");                               Label.Breakpoint("break_test3_nextline");

        Label.Checkpoint("break_test3", "break_test4",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "break_test3");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "break_test3_nextline");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test, that debugger ignore Debugger.Break() on step in case it already stop at step at this code.
        // Note, since test framework can't operate with columns in code line, we test that debugger stop at
        // step-step-step instead of step-step-break-step.
        int i = 0; Debugger.Break(); i++;                             Label.Breakpoint("break_test4");
        Console.WriteLine("Next Line i=" + i.ToString());             Label.Breakpoint("break_test4_nextline");

        Label.Checkpoint("break_test4", "break_test5",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "break_test4");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "break_test4");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "break_test4");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "break_test4_nextline");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test, that debugger stop at Debugger.Break() in managed code during step-over and reset step.
        test_func();                                                  Label.Breakpoint("break_test5");

        Label.Checkpoint("break_test5", "break_test5_func",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "break_test5");
                Context.StepOver(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }

    static void test_func()
    {
        Debugger.Break();                                             Label.Breakpoint("break_test5_func");
        Console.WriteLine("Test function.");

        Label.Checkpoint("break_test5_func", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakHit(@"__FILE__:__LINE__", "break_test5_func");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}
}
