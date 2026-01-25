using System;
using System.IO;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;
using Newtonsoft.Json;

namespace TestFuncBreak
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

                Context.AddFuncBreakpoint("funcbrackpoint1");
                Context.AddFuncBreakpoint("funcbrackpoint2(int)");
                Context.AddFuncBreakpoint("Program.funcbrackpoint3()");
                Context.AddFuncBreakpoint("funcbrackpoint4");
                Context.AddFuncBreakpoint("funcbrackpoint6(int)", "i==5");
                Context.AddFuncBreakpoint("TestFuncBreak.Program.funcbrackpoint7", "z<10");
                Context.SetFuncBreakpoints(@"__FILE__:__LINE__");

                // change condition to already setted bp
                Context.RemoveFuncBreakpoint("funcbrackpoint6(int)");
                Context.AddFuncBreakpoint("funcbrackpoint6(int)", "i>10");
                Context.SetFuncBreakpoints(@"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        funcbrackpoint1();
        funcbrackpoint2(5);
        funcbrackpoint3();
        funcbrackpoint4();
        funcbrackpoint5("funcbrackpoint5 test function");
        funcbrackpoint6(5);
        funcbrackpoint7(5);

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }

    static void funcbrackpoint1()
    {                                                                   Label.Breakpoint("br1");
        Console.WriteLine("funcbrackpoint1 test function");

        Label.Checkpoint("bp1_test", "bp2_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "br1");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static void funcbrackpoint2(int x)
    {                                                                   Label.Breakpoint("br2");
        Console.WriteLine("funcbrackpoint2 test function x=" + x.ToString());

        Label.Checkpoint("bp2_test", "bp3_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "br2");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static void funcbrackpoint3()
    {                                                                   Label.Breakpoint("br3");
        Console.WriteLine("funcbrackpoint3 test function");

        Label.Checkpoint("bp3_test", "bp5_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "br3");

                Context.RemoveFuncBreakpoint("funcbrackpoint4");
                Context.AddFuncBreakpoint("TestFuncBreak.Program.funcbrackpoint5(string)");
                Context.SetFuncBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static void funcbrackpoint4()
    {
        Console.WriteLine("funcbrackpoint4 test function");
    }

    static void funcbrackpoint5(string text)
    {                                                                   Label.Breakpoint("br5");
        Console.WriteLine(text);

        Label.Checkpoint("bp5_test", "bp7_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "br5");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static void funcbrackpoint6(int i)
    {
        Console.WriteLine("i=" + i.ToString());
    }

    static void funcbrackpoint7(int z)
    {                                                                   Label.Breakpoint("br7");
        Console.WriteLine("z=" + z.ToString());

        Label.Checkpoint("bp7_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "br7");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}
}
