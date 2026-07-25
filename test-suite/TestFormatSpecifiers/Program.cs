using System;
using System.IO;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestFormatSpecifiers
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

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testspecifiers",
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

        string testString = "test string";
        sbyte testSByte = -2;
        byte testByte = 1;
        short testShort = -3;
        ushort testUShort = 4;
        int testInt = -5;
        uint testUInt = 6;
        long testLong = -7;
        ulong testULong = 8;
        float testFloat = 9f;
        double testDouble = 10;
        decimal testDecimal = 11M;
        char testChar = 'ㅎ';

        TestClass1 testClass1 = new TestClass1();

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testspecifiers", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test string\"", "string", "testString");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "test string", "string", "testString,nq");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-2", "sbyte", "testSByte");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfe", "sbyte", "testSByte,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "byte", "testByte");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x01", "byte", "testByte,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3", "short", "testShort");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffd", "short", "testShort,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "4", "ushort", "testUShort");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x0004", "ushort", "testUShort,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-5", "int", "testInt");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffffffb", "int", "testInt,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "6", "uint", "testUInt");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x00000006", "uint", "testUInt,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-7", "long", "testLong");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffffffffffffff9", "long", "testLong,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "8", "ulong", "testULong");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x0000000000000008", "ulong", "testULong,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "9", "float", "testFloat");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "9", "float", "testFloat,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "double", "testDouble");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "double", "testDouble,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "decimal", "testDecimal");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "decimal", "testDecimal,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "12622 'ㅎ'", "char", "testChar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x314e 'ㅎ'", "char", "testChar,h");

                int variablesReference_testClass1 = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "y2", "3");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "x2", "4");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "SumOfFields", "11");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "i");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "j");

                int variablesReference_testClass1raw = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1,raw");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "int", "i", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "int", "j", "6");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "y1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "x1");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "y2");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "x2");
                Context.CheckErrorVariable(@"__FILE__:__LINE__", variablesReference_testClass1raw, "SumOfFields");


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
