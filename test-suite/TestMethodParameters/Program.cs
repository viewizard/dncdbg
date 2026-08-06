using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestMethodParameters
{

public class TestGeneric<X, Y>
{
}

class TestClass
{
    public void Abc(int i1, string s1, TestGeneric<int, string> u1)
    {
        ;                                                                      Label.Breakpoint("instance1");
    }

    public async Task<int> FetchDataAsync(int i1, string s1, TestGeneric<int, string> u1)
    {
        await Task.Delay(1000);
        return 5;                                                               Label.Breakpoint("instance3");
    }
}

class Program
{
    static void Bcd(string s2, int i2, TestGeneric<int, string> u2)
    {
        ;                                                                       Label.Breakpoint("static1");
    }

    static async Task<int> StaticFetchDataAsync(string s2, int i2, TestGeneric<int, string> u2)
    {
        await Task.Delay(1000);
        return 5;                                                               Label.Breakpoint("static3");
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_static",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: true, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "static1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "static3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test static
        TestGeneric<int, string> genParam = new TestGeneric<int, string>();
        Bcd("test", 1, genParam);                                               Label.Breakpoint("static2");
        int i = StaticFetchDataAsync("test", 1, genParam).GetAwaiter().GetResult();

        Label.Checkpoint("test_static", "test_instance",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "static1");
                string[] stacktrace = { "static1", "static2" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.Program.Bcd(string s2, int i2, TestMethodParameters.TestGeneric<int, string> u2)", stacktrace, 2);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "static3");
                string[] stacktrace2 = { "static3" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.Program.StaticFetchDataAsync(string s2, int i2, TestMethodParameters.TestGeneric<int, string> u2)", stacktrace2, 1);
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test instance
        TestClass testClass = new TestClass();
        testClass.Abc(1, "test", genParam);                                     Label.Breakpoint("instance2");
        i = testClass.FetchDataAsync(1, "test", genParam).GetAwaiter().GetResult();

        Label.Checkpoint("test_instance", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "instance1");
                string[] stacktrace = { "instance1", "instance2" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.TestClass.Abc(int i1, string s1, TestMethodParameters.TestGeneric<int, string> u1)", stacktrace, 2);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "instance3");
                string[] stacktrace2 = { "instance3" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.TestClass.FetchDataAsync(int i1, string s1, TestMethodParameters.TestGeneric<int, string> u1)", stacktrace2, 1);
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
