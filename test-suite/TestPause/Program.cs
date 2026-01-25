using System;
using System.IO;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestPause
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "pause_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.Pause(@"__FILE__:__LINE__");
            });

        System.Threading.Thread.Sleep(3000);

        Label.Checkpoint("pause_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasPaused(@"__FILE__:__LINE__");
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
