using System;
using System.IO;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestNoJMCAsyncStepping
{
class Program
{
    static async Task Main(string[] args)
    {
        Label.Checkpoint("init", "test_attr1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(false, null, @"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.StepOver(@"__FILE__:__LINE__");
            });

        // Test debugger attribute on methods with JMC disabled.

        await test_attr_func1();                                        Label.Breakpoint("test_attr_func1");
        await test_attr_func2();                                        Label.Breakpoint("test_attr_func2");
        await test_attr_func3();                                        Label.Breakpoint("test_attr_func3");

        Label.Checkpoint("test_attr1", "test_attr2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_func1");
                Context.StepIn(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_func2");
                Context.StepIn(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_func2_in");
                Context.StepOut(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_func2");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_func3");
                Context.StepIn(@"__FILE__:__LINE__");
            });

        // Test debugger attribute on class with JMC disabled.

        await ctest_attr1.test_func();                                  Label.Breakpoint("test_attr_class1_func");
        await ctest_attr2.test_func();                                  Label.Breakpoint("test_attr_class2_func");
        Console.WriteLine("Test debugger attribute on methods end.");   Label.Breakpoint("test_attr_end");

        Label.Checkpoint("test_attr2", "test_async_void",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_class1_func");
                Context.StepIn(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_class2_func");
                Context.StepIn(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_class2_func_in");
                Context.StepOut(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_class2_func");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_attr_end");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "test_async_void1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "test_async_void3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // Test `async void` stepping.

        await Task.Run((Action)(async () =>
            {
                await Task.Yield();                                        Label.Breakpoint("test_async_void1");
            }));                                                           Label.Breakpoint("test_async_void2");
        await Task.Delay(5000);
        Console.WriteLine("Test debugger `async void` stepping end."); Label.Breakpoint("test_async_void3");

        Label.Checkpoint("test_async_void", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "test_async_void1");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasStep(@"__FILE__:__LINE__", "test_async_void2");
                Context.StepOver(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "test_async_void3");
                Context.StepOut(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }

    [DebuggerStepThroughAttribute()]
    static async Task test_attr_func1()
    {
        await Task.Delay(1500);
    }

    [DebuggerNonUserCodeAttribute()]
    static async Task test_attr_func2()
    {                                                                   Label.Breakpoint("test_attr_func2_in");
        await Task.Delay(1500);
    }

    [DebuggerHiddenAttribute()]
    static async Task test_attr_func3()
    {
        await Task.Delay(1500);
    }
}

[DebuggerStepThroughAttribute()]
class ctest_attr1
{
    public static async Task test_func()
    {
        await Task.Delay(1500);
    }
}

[DebuggerNonUserCodeAttribute()]
class ctest_attr2
{
    public static async Task test_func()
    {                                                                   Label.Breakpoint("test_attr_class2_func_in");
        await Task.Delay(1500);
    }
}
}
