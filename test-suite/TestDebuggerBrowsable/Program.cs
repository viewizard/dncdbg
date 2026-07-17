using System;
using System.IO;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestDebuggerBrowsable
{

public class TestRootHiddenField
{
    public int val4 = 111;
    public int val5 = 222;
}

public class TestRootHiddenProperty
{
    public int val6 = 333;
    public int val7 = 444;
}

public class TestClass1
{
    public int val1
    {
        get {
            return 666;
        }
    }

    [System.Diagnostics.DebuggerBrowsable(DebuggerBrowsableState.Never)]
    public int val2
    {
        get {
            return 777;
        }
    }

    [System.Diagnostics.DebuggerBrowsable(DebuggerBrowsableState.Never)]
    public int val22 = 999;

    public int val3
    {
        get {
            return 888;
        }
    }

    [System.Diagnostics.DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public TestRootHiddenField f1 = new TestRootHiddenField();


    [System.Diagnostics.DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public TestRootHiddenProperty p1
    {
        get {
            return new TestRootHiddenProperty();
        }
    }
}

public class TestClass2
{
    [System.Diagnostics.DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public int[] Items
    {
        get
        {
            int[] items = new int[3];
            items[0] = 1;
            items[1] = 2;
            items[2] = 3;
            return items;
        }
    }
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testbrowsable",
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

        TestClass1 tc1 = new TestClass1();
        TestClass2 tc2 = new TestClass2();

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testbrowsable", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");

                int variablesReference_tc1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "tc1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val1", "666");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val3", "888");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val4", "111");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val5", "222");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val6", "333");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val7", "444");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val2");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val22");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "f1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "p1");

                int variablesReference_tc2 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "tc2");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc2, "int", "[0]", "1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc2, "int", "[1]", "2");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc2, "int", "[2]", "3");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "Items");

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
