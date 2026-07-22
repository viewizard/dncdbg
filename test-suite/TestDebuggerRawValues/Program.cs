using System;
using System.IO;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestDebuggerRawValues
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

[DebuggerTypeProxy(typeof(TestClassProxy2))]
class TestClass2
{
    public int i = 5;
    public int j = 6;

    static public int ii = 7;

    private class TestClassProxy2
    {
        private readonly TestClass2 _target;

        public TestClassProxy2(TestClass2 target)
        {
            _target = target;
        }

        private int y1 = 1;
        private int x1 => 2;
        public int y2 = 3;
        public int x2 => 4;

        static public int xx = 5;

        public int SumOfFields => _target.i + _target.j;
    }
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testrawvalues",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.expressionEvaluationOptions = new ExpressionEvaluationOptions();
                Context.expressionEvaluationOptions.showRawValues = true;
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

        Label.Checkpoint("testrawvalues", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");

                int variablesReference_tc1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "tc1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val1", "666");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val2", "777");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val22", "999");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "int", "val3", "888");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "TestDebuggerRawValues.TestRootHiddenField", "f1", "{TestDebuggerRawValues.TestRootHiddenField}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc1, "TestDebuggerRawValues.TestRootHiddenProperty", "p1", "{TestDebuggerRawValues.TestRootHiddenProperty}");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val4");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val5");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val6");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc1, "val7");

                int variablesReference_tc2 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "tc2");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc2, "int", "i", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_tc2, "int", "j", "6");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc2, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc2, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc2, "y2");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc2, "x2");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_tc2, "SumOfFields");

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
