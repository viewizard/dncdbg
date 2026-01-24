using System;
using System.IO;
using System.Collections.Generic;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestDisconnect
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.DebuggerExit(@"__FILE__:__LINE__");
                Context.WasExit(null, @"__FILE__:__LINE__");
            });

        System.Threading.Thread.Sleep(30000);
        // we should never reach this code
        Console.WriteLine("A breakpoint \"bp\" is set on this line"); Label.Breakpoint("bp");
    }
}
}
