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
        await Task.Delay(100);
        return 5;                                                               Label.Breakpoint("instance3");
    }
}

public class GenClass<X, Y>
{
    public void Ecd<W>(X i1, Y s1, W u1)
    {
        ;                                                                      Label.Breakpoint("instance4");
    }

    public async Task<int> FetchDataAsync<W>(X i1, Y s1, W u1)
    {
        await Task.Delay(100);
        return 5;                                                               Label.Breakpoint("instance6");
    }

    public static void Cde<W>(X s2, Y i2, W u2)
    {
        ;                                                                      Label.Breakpoint("static4");
    }

    public static async Task<int> StaticFetchDataAsync<W>(X s2, Y i2, W u2)
    {
        await Task.Delay(100);
        return 5;                                                               Label.Breakpoint("static6");
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
        await Task.Delay(100);
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
                Context.AddBreakpoint(@"__FILE__:__LINE__", "static4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "static6");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance6");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test static
        TestGeneric<int, string> genParam = new TestGeneric<int, string>();
        Bcd("test", 1, genParam);                                                   Label.Breakpoint("static2");
        int i = StaticFetchDataAsync("test", 1, genParam).GetAwaiter().GetResult();
        GenClass<string, int>.Cde<TestGeneric<int, string>>("test", 1, genParam);   Label.Breakpoint("static5");
        i = GenClass<string, int>.StaticFetchDataAsync<TestGeneric<int, string>>("test", 1, genParam).GetAwaiter().GetResult();

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

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "static4");
                string[] stacktrace3 = { "static4", "static5" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.GenClass<string, int>.Cde<TestMethodParameters.TestGeneric<int, string>>(string s2, int i2, TestMethodParameters.TestGeneric<int, string> u2)", stacktrace3, 2);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "static6");
                string[] stacktrace4 = { "static6" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.GenClass<string, int>.StaticFetchDataAsync<TestMethodParameters.TestGeneric<int, string>>(string s2, int i2, TestMethodParameters.TestGeneric<int, string> u2)", stacktrace4, 1);
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test instance
        TestClass testClass = new TestClass();
        testClass.Abc(1, "test", genParam);                                         Label.Breakpoint("instance2");
        i = testClass.FetchDataAsync(1, "test", genParam).GetAwaiter().GetResult();
        GenClass<int, string> genClass = new GenClass<int, string>();
        genClass.Ecd<TestGeneric<int, string>>(1, "test", genParam);                Label.Breakpoint("instance5");
        i = genClass.FetchDataAsync<TestGeneric<int, string>>(1, "test", genParam).GetAwaiter().GetResult();

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

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "instance4");
                string[] stacktrace3 = { "instance4", "instance5" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.GenClass<int, string>.Ecd<TestMethodParameters.TestGeneric<int, string>>(int i1, string s1, TestMethodParameters.TestGeneric<int, string> u1)", stacktrace3, 2);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "instance6");
                string[] stacktrace4 = { "instance6" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters.GenClass<int, string>.FetchDataAsync<TestMethodParameters.TestGeneric<int, string>>(int i1, string s1, TestMethodParameters.TestGeneric<int, string> u1)", stacktrace4, 1);
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
