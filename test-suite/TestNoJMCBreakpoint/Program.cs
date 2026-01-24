using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestNoJMCBreakpoint
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_attr1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(false, null, @"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");

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

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }

    [DebuggerStepThroughAttribute()]
    static void test_attr_func1()
    {                                                           Label.Breakpoint("BREAK_ATTR1");
        Label.Checkpoint("test_attr1", "test_attr2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK_ATTR1");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    [DebuggerNonUserCodeAttribute()]
    static void test_attr_func2()
    {                                                           Label.Breakpoint("BREAK_ATTR2");
        Label.Checkpoint("test_attr2", "test_attr4",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK_ATTR2");
                Context.Continue(@"__FILE__:__LINE__");
            });
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
        Label.Checkpoint("test_attr4", "test_attr5",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK_ATTR4");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}

[DebuggerNonUserCodeAttribute()]
class ctest_attr2
{
    public static void test_func()
    {                                                           Label.Breakpoint("BREAK_ATTR5");
        Label.Checkpoint("test_attr5", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK_ATTR5");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}
}
