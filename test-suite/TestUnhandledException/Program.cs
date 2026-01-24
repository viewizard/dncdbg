using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestUnhandledException
{
class Program
{
    static void Abc()
    {
        throw new Exception();                                                  Label.Breakpoint("throwexception");
    }

    static async Task Main(string[] args)
    {
        Label.Checkpoint("init", "test_unhandled",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(true, null, @"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test "unhandled"
        await Task.Yield();
        Abc();                                                                  Label.Breakpoint("callabc");

        Label.Checkpoint("test_unhandled", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                string[] stacktrace = { "throwexception", "callabc" };
                Context.TestExceptionStackTrace(@"__FILE__:__LINE__", stacktrace, 2);
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                // At this point debugger stops at unhandled exception, no reason continue process, abort execution.
                Context.AbortExecution(@"__FILE__:__LINE__");
                Context.WasExit(null, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
