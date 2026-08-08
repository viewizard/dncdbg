using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestDebuggerDisplay
{

[DebuggerDisplay("Test eval={Test() + i + 5,h}")]
class TestClass1
{
    public int i = 5;
    public int Test()
    {
        return 5;
    }
}

[DebuggerDisplay("Test enum")]
public enum TestEnum
{
    append = 3,
    write = 2,
    read = 1,
    None = 0 // legit code
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testdisplay",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        TestClass1 testClass1 = new TestClass1();
        List<int> list1 = new List<int>(5) {10, 20, 30, 40, 50};
        Dictionary<string, int> dictionary1 = new Dictionary<string, int>(){ { "Alice", 25 }, { "Bob", 30 } };
        TestEnum testEnum = TestEnum.append;

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testdisplay", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_Locals, "TestDebuggerDisplay.TestClass1", "testClass1", "Test eval=0x0000000f");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_Locals, "System.Collections.Generic.List<int>", "list1", "Count = 5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_Locals, "System.Collections.Generic.Dictionary<string, int>", "dictionary1", "Count = 2");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_Locals, "TestDebuggerDisplay.TestEnum", "testEnum", "Test enum");

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
