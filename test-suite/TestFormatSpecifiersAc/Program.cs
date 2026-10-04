using System;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestFormatSpecifiersAc
{

class TestClass1
{
    public int i => 5;
    public int j => 6;

    public int ii = 7;

    public static int si = 5;
    public static int sj = 7;
}

class TestClass2
{
    public static int si = 5;
    public static int sj = 7;
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "teststatic",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.expressionEvaluationOptions = new ExpressionEvaluationOptions();
                Context.expressionEvaluationOptions.allowImplicitFuncEval = false;
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("teststatic", "testspecifiers",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                int variablesReference_testClass1 = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1");
                int setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass1, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "si", "<error>");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "sj", "<error>");

                int variablesReference_testClass2 = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass2");
                setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass2, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "si", "<error>");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "sj", "<error>");

                Context.Continue(@"__FILE__:__LINE__");
            });

        TestClass1 testClass1 = new TestClass1();
        TestClass2 testClass2;
        TestClass1.si = 25;

        int j = 1;                                                Label.Breakpoint("bp2");

        Label.Checkpoint("testspecifiers", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp2");

                int variablesReference_testClass1 = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "", "i", "<error>");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "", "j", "<error>");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1, "int", "ii", "7");
                int setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass1, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "si", "25");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "sj", "7");

                int variablesReference_testClass1ac = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1,ac");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ac, "int", "i", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ac, "int", "j", "6");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ac, "int", "ii", "7");
                setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass1ac, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "si", "25");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "sj", "7");

                int variablesReference_testClass1ach = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1,ac,h");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ach, "int", "i", "0x00000005");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ach, "int", "j", "0x00000006");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1ach, "int", "ii", "0x00000007");

                int variablesReference_testClass1hac = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass1,h,ac");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1hac, "int", "i", "0x00000005");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1hac, "int", "j", "0x00000006");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_testClass1hac, "int", "ii", "0x00000007");

                int variablesReference_testClass2 = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass2");
                setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass2, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "si", "<error>");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "", "sj", "<error>");

                int variablesReference_testClass2ac = Context.GetExpressionEvaluationReference(@"__FILE__:__LINE__", frameId, "testClass2,ac");
                setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass2ac, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "si", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "sj", "7");

                setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_testClass2, "Static members");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "si", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", setStaticReference, "int", "sj", "7");

                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
