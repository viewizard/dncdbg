using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestRestartLaunch
{

class Program
{
    static void TestFunc()
    {                                                                 Label.Breakpoint("FUNC_BREAK1");

    }

    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "restart_on_breakpoint_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddFunctionBreakpoint("TestFunc");
                Context.SetFunctionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK1");
            });

        TestFunc();
        ;                                                                 Label.Breakpoint("BREAK1");

        Label.Checkpoint("restart_on_breakpoint_test", "restart_after_exit_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Restart(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(0, @"__FILE__:__LINE__");
            });

        Label.Checkpoint("restart_after_exit_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Restart(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "FUNC_BREAK1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Context.Continue(@"__FILE__:__LINE__");
            });


        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
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
