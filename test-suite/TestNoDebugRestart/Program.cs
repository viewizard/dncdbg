using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestNoDebugRestart
{

class Program
{
    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "restart_on_sleep_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", NoDebug: true);
            });

        Console.WriteLine("NoDebug application start");
        System.Threading.Thread.Sleep(4000);
        Console.WriteLine("NoDebug application end");

        Label.Checkpoint("restart_on_sleep_test", "restart_after_exit_test",
            (Object context) =>
            {
                System.Threading.Thread.Sleep(1000);
                Context Context = (Context)context;
                Context.Restart(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: 1);
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("restart_after_exit_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Restart(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
