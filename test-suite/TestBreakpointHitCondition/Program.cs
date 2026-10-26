using System;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestBreakpointHitCondition
{
class Program
{
    static void func1()
    {                                                             Label.Breakpoint("func1");
    }
    static void func2()
    {                                                             Label.Breakpoint("func2");
    }
    static void func3()
    {                                                             Label.Breakpoint("func3");
    }
    static void func4()
    {                                                             Label.Breakpoint("func4");
    }
    static void func5()
    {                                                             Label.Breakpoint("func5");
    }
    static void func6()
    {                                                             Label.Breakpoint("func6");
    }
    static void func7()
    {                                                             Label.Breakpoint("func7");
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "loop1_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1", hitCondition: "3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2", hitCondition: "<2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp3", hitCondition: "<=3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4", hitCondition: "==5");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp5", hitCondition: ">4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp6", hitCondition: ">=6");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp7", hitCondition: "%2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func1", HitCondition: "3");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func2", HitCondition: "<2");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func3", HitCondition: "<=3");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func4", HitCondition: "==5");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func5", HitCondition: ">4");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func6", HitCondition: ">=6");
                Context.AddFunctionBreakpoint("TestBreakpointHitCondition.Program.func7", HitCondition: "%2");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        for (int i = 0; i < 7; ++i)
        {
            ;                                                     Label.Breakpoint("bp1");
            ;                                                     Label.Breakpoint("bp2");
            ;                                                     Label.Breakpoint("bp3");
            ;                                                     Label.Breakpoint("bp4");
            ;                                                     Label.Breakpoint("bp5");
            ;                                                     Label.Breakpoint("bp6");
            ;                                                     Label.Breakpoint("bp7");
        }

        Label.Checkpoint("loop1_test", "loop2_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop2_test", "loop3_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop3_test", "loop4_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop4_test", "loop5_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop5_test", "loop6_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp4");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop6_test", "loop7_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp6");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("loop7_test", "func_loop1_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp6");
                Context.Continue(@"__FILE__:__LINE__");
            });

        for (int i = 0; i < 7; ++i)
        {
            func1();
            func2();
            func3();
            func4();
            func5();
            func6();
            func7();
        }

        Label.Checkpoint("func_loop1_test", "func_loop2_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func3");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop2_test", "func_loop3_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop3_test", "func_loop4_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func3");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop4_test", "func_loop5_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop5_test", "func_loop6_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func4");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func5");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop6_test", "func_loop7_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func6");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func7");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("func_loop7_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "func6");
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
