using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

[assembly: DebuggerTypeProxy(typeof(TestDebuggerTypeProxy.TestClass8.TestClassProxy8), Target = typeof(TestDebuggerTypeProxy.TestClass8))]
[assembly: DebuggerTypeProxy("TestDebuggerTypeProxy.TestClass9+TestClassProxy9", TargetTypeName = "TestDebuggerTypeProxy.TestClass9")]

namespace TestDebuggerTypeProxy
{

[DebuggerTypeProxy(typeof(TestClassProxy1))]
class TestClass1
{
    public int i = 5;
    public int j = 6;

    static public int ii = 7;

    private class TestClassProxy1
    {
        private readonly TestClass1 _target;

        public TestClassProxy1(TestClass1 target)
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

[DebuggerTypeProxy("TestClassProxy2")]
class TestClass2
{
    public int i = 50;
    public int j = 60;

    class TestClassProxy2
    {
        private readonly TestClass2 _target;

        public TestClassProxy2(TestClass2 target)
        {
            _target = target;
        }

        private int y1 = 10;
        private int x1 => 20;
        public int y2 = 30;
        public int x2 => 40;

        public int SumOfFields => _target.i + _target.j;
    }
}

[DebuggerTypeProxy(typeof(TestClassProxy3<>))]
class TestClass3<T>
{
    public int i = 5;
    public int j = 6;

    private class TestClassProxy3<M>
    {
        private readonly TestClass3<M> _target;

        public TestClassProxy3(TestClass3<M> target)
        {
            _target = target;
        }

        private int y1 = 1;
        private int x1 => 2;
        public int y2 = 3;
        public int x2 => 4;

        public int SumOfFields => _target.i + _target.j;
    }
}

[DebuggerTypeProxy("TestClassProxy4`1")]
class TestClass4<T>
{
    public int i = 50;
    public int j = 60;

    private class TestClassProxy4<M>
    {
        private readonly TestClass4<M> _target;

        public TestClassProxy4(TestClass4<M> target)
        {
            _target = target;
        }

        private int y1 = 10;
        private int x1 => 20;
        public int y2 = 30;
        public int x2 => 40;

        public int SumOfFields => _target.i + _target.j;
    }
}

[DebuggerTypeProxy(typeof(TestClassProxy5<>))]
class TestClass5<T>
{
    public int i = 5;
    public int j = 6;
}
class TestClassProxy5<M>
{
    private readonly TestClass5<M> _target;

    public TestClassProxy5(TestClass5<M> target)
    {
        _target = target;
    }

    private int y1 = 1;
    private int x1 => 2;
    public int y2 = 3;
    public int x2 => 4;

    public int SumOfFields => _target.i + _target.j;
}

[DebuggerTypeProxy("TestDebuggerTypeProxy.TestClass6`2+NestedClass`2+TestClassProxy6`2, TestDebuggerTypeProxy")]
class TestClass6<T1,T2>
{
    public int i = 500;
    public int j = 600;

    private class NestedClass<N1,N2>
    {
        private class TestClassProxy6<M1,M2>
        {
            private readonly TestClass6<M1,M2> _target;

            public TestClassProxy6(TestClass6<M1,M2> target)
            {
                _target = target;
            }

            private int y1 = 100;
            private int x1 => 200;
            public int y2 = 300;
            public int x2 => 400;

            public int SumOfFields => _target.i + _target.j;
        }
    }
}

// Test for DebuggerTypeProxy inheritance from base class.
public class MyTestList1<T> : List<T>
{
    public MyTestList1() : base()
    {
    }

    public MyTestList1(int capacity) : base(capacity)
    {
    }

    public MyTestList1(IEnumerable<T> collection) : base(collection)
    {
    }

    public new void Add(T item)
    {
        base.Add(item); 
    }

    public int i = 500;
    public int j = 600;
}

// Test for DebuggerTypeProxy inheritance from base class.
class TestClass7<T> : TestClass3<T>
{
    public int ii = 5;
    public int jj = 6;

    public TestClass7() : base()
    {
    }
}

// Test assembly DebuggerTypeProxy
class TestClass8
{
    public int i = 5;
    public int j = 6;

    static public int ii = 7;

    internal class TestClassProxy8
    {
        private readonly TestClass8 _target;

        public TestClassProxy8(TestClass8 target)
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

// Test assembly DebuggerTypeProxy
class TestClass9
{
    public int i = 50;
    public int j = 60;

    class TestClassProxy9
    {
        private readonly TestClass9 _target;

        public TestClassProxy9(TestClass9 target)
        {
            _target = target;
        }

        private int y1 = 10;
        private int x1 => 20;
        public int y2 = 30;
        public int x2 => 40;

        public int SumOfFields => _target.i + _target.j;
    }
}


class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testtypeproxy",
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
        TestClass1 testClass1Null;
        TestClass2 testClass2 = new TestClass2();
        TestClass3<string> testClass3 = new TestClass3<string>();
        TestClass4<string> testClass4 = new TestClass4<string>();
        TestClass5<string> testClass5 = new TestClass5<string>();
        TestClass6<int, float> testClass6 = new TestClass6<int, float>();
        List<int> list1 = new List<int>(5) {10, 20, 30, 40, 50};
        Dictionary<string, int> dictionary1 = new Dictionary<string, int>(){ { "Alice", 25 }, { "Bob", 30 } };
        MyTestList1<int> myTestList1 = new MyTestList1<int>(5) {10, 20, 30, 40, 50};
        TestClass7<string> testClass7 = new TestClass7<string>();
        TestClass8 testClass8 = new TestClass8();
        TestClass9 testClass9 = new TestClass9();

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testtypeproxy", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");

                int variablesReference_testClass1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "j");

                int variablesReference_testClass1Null = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass1Null");
                int variablesReference_testClass1NullStaticMembers = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass1Null, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1NullStaticMembers, "int", "ii", "7");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1NullStaticMembers, "xx");

                int variablesReference_testClass2 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass2");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "int", "y2", "30");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "int", "x2", "40");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "int", "SumOfFields", "110");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass2, "j");

                int variablesReference_testClass3 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass3, "j");

                int variablesReference_testClass4 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "int", "y2", "30");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "int", "x2", "40");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "int", "SumOfFields", "110");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass4, "j");

                int variablesReference_testClass5 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass5, "j");

                int variablesReference_testClass6 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass6");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "int", "y2", "300");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "int", "x2", "400");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "int", "SumOfFields", "1100");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass6, "j");

                int variablesReference_list1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "list1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_list1, "int", "[0]", "10");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_list1, "int", "[1]", "20");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_list1, "int", "[2]", "30");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_list1, "int", "[3]", "40");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_list1, "int", "[4]", "50");

                int variablesReference_dictionary1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "dictionary1");
                int variablesReference_dictionary1_0 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_dictionary1, "[0]");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_dictionary1_0, "string", "Key", "\"Alice\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_dictionary1_0, "int", "Value", "25");
                int variablesReference_dictionary1_1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_dictionary1, "[1]");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_dictionary1_1, "string", "Key", "\"Bob\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_dictionary1_1, "int", "Value", "30");

                int variablesReference_myTestList1 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "myTestList1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "int", "[0]", "10");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "int", "[1]", "20");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "int", "[2]", "30");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "int", "[3]", "40");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "int", "[4]", "50");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_myTestList1, "j");

                int variablesReference_testClass7 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass7");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "j");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "ii");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass7, "jj");

                int variablesReference_testClass8 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass8");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass8, "j");

                int variablesReference_testClass9 = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "testClass9");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "int", "y2", "30");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "int", "x2", "40");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "int", "SumOfFields", "110");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass9, "j");

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
