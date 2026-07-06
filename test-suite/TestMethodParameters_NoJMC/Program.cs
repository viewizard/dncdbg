using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestMethodParameters_NoJMC
{

class TestClass
{
    public void Abc(int i1, string s1, uint u1)
    {
        ;                                                                      Label.Breakpoint("instance1");
    }
}

class Program
{
    static void Bcd(string s2, int i2, uint u2)
    {
        ;                                                                       Label.Breakpoint("static1");
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_static",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: false, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "static1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "instance1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test static
        Bcd("test", 1, 2);                                                      Label.Breakpoint("static2");

        Label.Checkpoint("test_static", "test_instance",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "static1");
                string[] stacktrace = { "static1", "static2" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters_NoJMC.Program.Bcd(string s2, int i2, uint u2)", stacktrace, 2);
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test instance
        TestClass testClass = new TestClass();
        testClass.Abc(1, "test", 2);                                            Label.Breakpoint("instance2");

        Label.Checkpoint("test_instance", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "instance1");
                string[] stacktrace = { "instance1", "instance2" };
                Context.TestStackTrace(@"__FILE__:__LINE__", "TestMethodParameters_NoJMC.TestClass.Abc(int i1, string s1, uint u1)", stacktrace, 2);

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
