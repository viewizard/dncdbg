using System;
using System.IO;
using System.Threading;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestSingleThreadExec
{
class Program
{
    static void DoWork()
    {
        System.Threading.Thread.Sleep(4000);
        ;                                                                 Label.Breakpoint("BREAK1");
    }

    static void TestMethod()
    {                                                                     Label.Breakpoint("test_step_3");
        System.Threading.Thread.Sleep(4000);                              Label.Breakpoint("test_step_4");
        System.Threading.Thread.Sleep(4000);                              Label.Breakpoint("test_step_5");
    }

    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "single_thread_exec_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        System.Threading.Thread testThread = new System.Threading.Thread(DoWork);
        testThread.Start();
        System.Threading.Thread.Sleep(1000);

        ;                                                                 Label.Breakpoint("BREAK2");

        System.Threading.Thread.Sleep(4000);                              Label.Breakpoint("test_step_1");
        TestMethod();                                                     Label.Breakpoint("test_step_2");

        System.Threading.Thread.Sleep(4000);

        ;                                                                 Label.Breakpoint("BREAK3");

        testThread.Join();

        Label.Checkpoint("single_thread_exec_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK2");

                Context.StepOver(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_1");
                Context.StepOver(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_2");
                Context.StepIn(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_3");
                Context.StepIn(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_4");
                Context.StepIn(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_5");
                Context.StepOut(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasStep(@"__FILE__:__LINE__", "test_step_2");

                Context.Continue(@"__FILE__:__LINE__", SingleThread: true);
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK3");

                Context.RemoveBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

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

    struct TestStruct
    {
        public int a;
        public int b;

        public TestStruct(int x, int y)
        {
            a = x;
            b = y;
        }
    }
}
}
