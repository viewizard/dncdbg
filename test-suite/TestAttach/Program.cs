using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestAttach
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "bp_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStartAttach(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
            });

        // wait some time, control process should attach and setup breakpoints
        int i = 3000;
        System.Threading.Thread.Sleep(3000);

        i++;                                                        Label.Breakpoint("bp");

        Label.Checkpoint("bp_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp");
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
