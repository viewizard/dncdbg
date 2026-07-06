using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestUnhandledExceptionStatic
{
class Program
{
    static void Abc(int i, string s, uint u)
    {
        throw new Exception();                                                  Label.Breakpoint("throwexception");
    }

    static async Task Main(string[] args)
    {
        Label.Checkpoint("init", "test_unhandled",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: true, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test "unhandled"
        await Task.Yield();
        Abc(1, "test", 2);                                                      Label.Breakpoint("callabc");

        Label.Checkpoint("test_unhandled", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                string[] stacktrace = { "throwexception", "callabc" };
                Context.TestExceptionStackTrace(@"__FILE__:__LINE__", "[Exception] TestUnhandledExceptionStatic.Program.Abc(int i, string s, uint u)", stacktrace, 2);
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                // At this point debugger stops at unhandled exception, no reason to continue process, abort execution.
                Context.AbortExecution(@"__FILE__:__LINE__");
                Context.WasExit(null, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
