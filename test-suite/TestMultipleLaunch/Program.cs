using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestMultipleLaunch
{

class Program
{
    static void TestFunc1()
    {                                                                 Label.Breakpoint("FUNC_BREAK1");

    }

    static void TestFunc2()
    {                                                                 Label.Breakpoint("FUNC_BREAK2");

    }

    static void TestFunc3()
    {                                                                 Label.Breakpoint("FUNC_BREAK3");

    }

    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "launch_on_breakpoint_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFunctionBreakpoint("TestFunc1");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK1");
            });

        TestFunc1();
        ;                                                                 Label.Breakpoint("BREAK1");
        TestFunc2();
        ;                                                                 Label.Breakpoint("BREAK2");
        TestFunc3();
        ;                                                                 Label.Breakpoint("BREAK3");

        Label.Checkpoint("launch_on_breakpoint_test", "launch_after_exit_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.AbortExecution(@"__FILE__:__LINE__");
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFunctionBreakpoint("TestFunc2");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(0, @"__FILE__:__LINE__");
            });

        Label.Checkpoint("launch_after_exit_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFunctionBreakpoint("TestFunc3");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(0, @"__FILE__:__LINE__");
            });

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
