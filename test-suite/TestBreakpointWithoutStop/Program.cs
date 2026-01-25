using System;
using System.IO;
using System.Threading;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestBreakpointWithoutStop
{
class Program
{
    static void testfunc()
    {                                                                           Label.Breakpoint("bp_func");
        Console.WriteLine("A breakpoint is set on this testfunc");
    }

    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "bp_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                System.Threading.Thread.Sleep(5000);

                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFuncBreakpoint("testfunc");
                Context.SetFuncBreakpoints(@"__FILE__:__LINE__");
            });

        System.Threading.Thread.Sleep(15000);

        Console.WriteLine("A breakpoint \"bp\" is set on this line");           Label.Breakpoint("bp");

        Label.Checkpoint("bp_test", "bp_test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp");
                Context.Continue(@"__FILE__:__LINE__");
            });

        testfunc();

        Label.Checkpoint("bp_test2", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_func");
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
