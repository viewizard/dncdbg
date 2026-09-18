using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestAttachToSuspend
{
class Program
{
    static void Main(string[] args)
    {                                                                Label.Breakpoint("bp");
        Label.Checkpoint("init", "attach_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.StartTargetAndAttach(@"__FILE__:__LINE__", StartSuspend: true);
                Context.AddFunctionBreakpoint("Main");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("attach_test", "finish",
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
