using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestBreakpoint
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "bp1_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_cond_fail_1", "i"); // test condition: return not boolean value
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_cond_fail_2", "a != b"); // test condition: variable not exist in the current context
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_cond_fail_3", "method_with_exc()"); // test condition: exception during evaluation
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("A breakpoint \"bp1\" is set on this line"); Label.Breakpoint("bp1");

        Label.Checkpoint("bp1_test", "bp3_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");

                Context.RemoveBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("A breakpoint \"bp2\" is set on this line and removed"); Label.Breakpoint("bp2");

        Console.WriteLine("A breakpoint \"bp3\" is set on this line"); Label.Breakpoint("bp3");

        Label.Checkpoint("bp3_test", "bp5_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4", "i==5");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp5", "i>10 || z==0");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                // change condition to already setted bp
                Context.RemoveBreakpoint(@"__FILE__:__LINE__", "bp4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4", "i>10");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 5;
        Console.WriteLine("A breakpoint \"bp4\" is set on this line, i=" + i.ToString()); Label.Breakpoint("bp4");

        int z = 0;
        Console.WriteLine("A breakpoint \"bp5\" is set on this line, z=" + z.ToString()); Label.Breakpoint("bp5");

        Label.Checkpoint("bp5_test", "bp6_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK_ATTR1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK_ATTR2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK_ATTR3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK_ATTR4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK_ATTR5");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        test_attr_func1();
        test_attr_func2();
        test_attr_func3();
        ctest_attr1.test_func();
        ctest_attr2.test_func();

        // Test breakpoints with condition evaluation fails.

        ;                                       Label.Breakpoint("bp_cond_fail_1");
        ;                                       Label.Breakpoint("bp_cond_fail_2");
        ;                                       Label.Breakpoint("bp_cond_fail_3");

        Label.Checkpoint("bp6_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_cond_fail_1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_cond_fail_2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_cond_fail_3");
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

    static bool method_with_exc()
    {
        throw new System.Exception();
    }

    [DebuggerStepThroughAttribute()]
    static void test_attr_func1()
    {                                                           Label.Breakpoint("BREAK_ATTR1");
    }

    [DebuggerNonUserCodeAttribute()]
    static void test_attr_func2()
    {                                                           Label.Breakpoint("BREAK_ATTR2");
    }

    [DebuggerHiddenAttribute()]
    static void test_attr_func3()
    {                                                           Label.Breakpoint("BREAK_ATTR3");
    }
}

[DebuggerStepThroughAttribute()]
class ctest_attr1
{
    public static void test_func()
    {                                                           Label.Breakpoint("BREAK_ATTR4");
    }
}

[DebuggerNonUserCodeAttribute()]
class ctest_attr2
{
    public static void test_func()
    {                                                           Label.Breakpoint("BREAK_ATTR5");
    }
}
}
