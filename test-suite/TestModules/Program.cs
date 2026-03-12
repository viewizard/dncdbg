using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestModules
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "modules1_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("Hello world!");
        ;                                                       Label.Breakpoint("bp1");

        Label.Checkpoint("modules1_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");

                Context.TestModules(@"__FILE__:__LINE__", 100, 0, false);
                Context.TestModules(@"__FILE__:__LINE__", 0, 100, true);
                Context.TestModules(@"__FILE__:__LINE__", 0, 0, true);
                Context.TestModules(@"__FILE__:__LINE__", 100, 100, false);
                Context.TestModules(@"__FILE__:__LINE__", 3, 1, false);

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
