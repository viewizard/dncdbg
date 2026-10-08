using System;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestThreadStatic
{

public struct TestStaticStruct
{

}

public class TestStaticClass
{

}

public struct TestStaticStruct2
{
    public int i;
}

public class TestStaticClass2
{
    public int i = 5;
}

public class ParentClass
{
    public struct TestStaticStruct3
    {
        public int i;
    }

    public class TestStaticClass3
    {
        public int i = 5;
    }
}

public class NoCctor
{
        public static int s_int;
        public static volatile int vs_int;
        public static nint s_nint;
        public static decimal s_dec;
        public static string s_string;
        public static int[] s_array;
        public static int[,,] s_array2;
        public static TestStaticStruct s_struct;
        public static TestStaticStruct2 s_struct2;
        public static ParentClass.TestStaticStruct3 s_struct3;
        public static TestStaticClass s_class;
        public static TestStaticClass2 s_class2;
        public static ParentClass.TestStaticClass3 s_class3;

        [ThreadStatic] public static int t_int;
        [ThreadStatic] public static volatile int vt_int;
        [ThreadStatic] public static nint t_nint;
        [ThreadStatic] public static decimal t_dec;
        [ThreadStatic] public static string t_string;
        [ThreadStatic] public static int[] t_array;
        [ThreadStatic] public static int[,,] t_array2;
        [ThreadStatic] public static TestStaticStruct t_struct;
        [ThreadStatic] public static TestStaticStruct2 t_struct2;
        [ThreadStatic] public static ParentClass.TestStaticStruct3 t_struct3;
        [ThreadStatic] public static TestStaticClass t_class;
        [ThreadStatic] public static TestStaticClass2 t_class2;
        [ThreadStatic] public static ParentClass.TestStaticClass3 t_class3;
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "main1_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.AddEnvMapEntry("DNCDBG_MEMBERS_PER_PAGE_LIMIT", "100");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        ;                                                                 Label.Breakpoint("BREAK1");
        NoCctor noCctor;

        Label.Checkpoint("main1_test", "main2_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.s_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.vs_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "nint", "TestThreadStatic.NoCctor.s_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "decimal", "TestThreadStatic.NoCctor.s_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "TestThreadStatic.NoCctor.s_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[]", "TestThreadStatic.NoCctor.s_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[,,]", "TestThreadStatic.NoCctor.s_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.s_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.s_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.s_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.s_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.s_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.s_class3");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.t_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.vt_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "nint", "TestThreadStatic.NoCctor.t_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "decimal", "TestThreadStatic.NoCctor.t_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "TestThreadStatic.NoCctor.t_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[]", "TestThreadStatic.NoCctor.t_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[,,]", "TestThreadStatic.NoCctor.t_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.t_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.t_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.t_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.t_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.t_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.t_class3");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                int variablesReference_noCctor = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "noCctor");
                int variablesReference_StaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_noCctor, "Static members");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "s_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vs_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "s_nint", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "s_dec", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "s_string", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "s_array", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "s_array2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "s_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "s_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "s_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "s_class", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "s_class2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "s_class3", "null");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "t_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vt_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "t_nint", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "t_dec", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "t_string", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "t_array", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "t_array2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "t_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "t_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "t_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "t_class", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "t_class2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "t_class3", "null");

                Context.Continue(@"__FILE__:__LINE__");
            });

        TestThreadStatic.NoCctor.s_int = 5;
        TestThreadStatic.NoCctor.vs_int = 6;
        TestThreadStatic.NoCctor.s_nint = 7;
        TestThreadStatic.NoCctor.s_dec = 8;
        TestThreadStatic.NoCctor.s_string = "test";
        TestThreadStatic.NoCctor.s_array = new int[] { 1, 2 };
        TestThreadStatic.NoCctor.s_array2 = new int[,,]
                                            {
                                                { { 1, 2 }, { 3, 4 } },
                                                { { 5, 6 }, { 7, 8 } }
                                            };
        TestThreadStatic.NoCctor.s_struct = new TestStaticStruct();
        TestThreadStatic.NoCctor.s_struct2 = new TestStaticStruct2();
        TestThreadStatic.NoCctor.s_struct3 = new ParentClass.TestStaticStruct3();
        TestThreadStatic.NoCctor.s_class = new TestStaticClass();
        TestThreadStatic.NoCctor.s_class2 = new TestStaticClass2();
        TestThreadStatic.NoCctor.s_class3 = new ParentClass.TestStaticClass3();

        TestThreadStatic.NoCctor.t_int = 15;
        TestThreadStatic.NoCctor.vt_int = 16;
        TestThreadStatic.NoCctor.t_nint = 17;
        TestThreadStatic.NoCctor.t_dec = 18;
        TestThreadStatic.NoCctor.t_string = "test2";
        TestThreadStatic.NoCctor.t_array = new int[] { 1, 2 };
        TestThreadStatic.NoCctor.t_array2 = new int[,,]
                                            {
                                                { { 1, 2 }, { 3, 4 } },
                                                { { 5, 6 }, { 7, 8 } }
                                            };
        TestThreadStatic.NoCctor.t_struct = new TestStaticStruct();
        TestThreadStatic.NoCctor.t_struct2 = new TestStaticStruct2();
        TestThreadStatic.NoCctor.t_struct3 = new ParentClass.TestStaticStruct3();
        TestThreadStatic.NoCctor.t_class = new TestStaticClass();
        TestThreadStatic.NoCctor.t_class2 = new TestStaticClass2();
        TestThreadStatic.NoCctor.t_class3 = new ParentClass.TestStaticClass3();

        ;                                                                 Label.Breakpoint("BREAK2");

        Label.Checkpoint("main2_test", "thread_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK2");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK2");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "TestThreadStatic.NoCctor.s_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "6", "int", "TestThreadStatic.NoCctor.vs_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "7", "nint", "TestThreadStatic.NoCctor.s_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "8", "decimal", "TestThreadStatic.NoCctor.s_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test\"", "string", "TestThreadStatic.NoCctor.s_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2]}", "int[]", "TestThreadStatic.NoCctor.s_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2, 2, 2]}", "int[,,]", "TestThreadStatic.NoCctor.s_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.s_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.s_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.s_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass}", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.s_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass2}", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.s_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticClass3}", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.s_class3");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "TestThreadStatic.NoCctor.t_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "16", "int", "TestThreadStatic.NoCctor.vt_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "17", "nint", "TestThreadStatic.NoCctor.t_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "18", "decimal", "TestThreadStatic.NoCctor.t_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test2\"", "string", "TestThreadStatic.NoCctor.t_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2]}", "int[]", "TestThreadStatic.NoCctor.t_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2, 2, 2]}", "int[,,]", "TestThreadStatic.NoCctor.t_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.t_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.t_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.t_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass}", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.t_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass2}", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.t_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticClass3}", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.t_class3");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                int variablesReference_noCctor = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "noCctor");
                int variablesReference_StaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_noCctor, "Static members");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "s_int", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vs_int", "6");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "s_nint", "7");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "s_dec", "8");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "s_string", "\"test\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "s_array", "{int[2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "s_array2", "{int[2, 2, 2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "s_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "s_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "s_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "s_class", "{TestThreadStatic.TestStaticClass}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "s_class2", "{TestThreadStatic.TestStaticClass2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "s_class3", "{TestThreadStatic.ParentClass.TestStaticClass3}");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "t_int", "15");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vt_int", "16");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "t_nint", "17");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "t_dec", "18");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "t_string", "\"test2\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "t_array", "{int[2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "t_array2", "{int[2, 2, 2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "t_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "t_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "t_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "t_class", "{TestThreadStatic.TestStaticClass}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "t_class2", "{TestThreadStatic.TestStaticClass2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "t_class3", "{TestThreadStatic.ParentClass.TestStaticClass3}");

                Context.Continue(@"__FILE__:__LINE__");
            });

        System.Threading.Thread threadWorker = new System.Threading.Thread(ThreadWorker);
        threadWorker.Start();
        threadWorker.Join();

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }

    static void ThreadWorker()
    {
        ;                                                                 Label.Breakpoint("BREAK3");
        NoCctor noCctor;

        Label.Checkpoint("thread_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK3");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK3");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "TestThreadStatic.NoCctor.s_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "6", "int", "TestThreadStatic.NoCctor.vs_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "7", "nint", "TestThreadStatic.NoCctor.s_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "8", "decimal", "TestThreadStatic.NoCctor.s_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test\"", "string", "TestThreadStatic.NoCctor.s_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2]}", "int[]", "TestThreadStatic.NoCctor.s_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{int[2, 2, 2]}", "int[,,]", "TestThreadStatic.NoCctor.s_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.s_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.s_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.s_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass}", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.s_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticClass2}", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.s_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticClass3}", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.s_class3");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.t_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "TestThreadStatic.NoCctor.vt_int");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "nint", "TestThreadStatic.NoCctor.t_nint");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "decimal", "TestThreadStatic.NoCctor.t_dec");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "TestThreadStatic.NoCctor.t_string");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[]", "TestThreadStatic.NoCctor.t_array");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "int[,,]", "TestThreadStatic.NoCctor.t_array2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct}", "TestThreadStatic.TestStaticStruct", "TestThreadStatic.NoCctor.t_struct");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.TestStaticStruct2}", "TestThreadStatic.TestStaticStruct2", "TestThreadStatic.NoCctor.t_struct2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestThreadStatic.ParentClass.TestStaticStruct3}", "TestThreadStatic.ParentClass.TestStaticStruct3", "TestThreadStatic.NoCctor.t_struct3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass", "TestThreadStatic.NoCctor.t_class");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.TestStaticClass2", "TestThreadStatic.NoCctor.t_class2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestThreadStatic.ParentClass.TestStaticClass3", "TestThreadStatic.NoCctor.t_class3");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                int variablesReference_noCctor = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_Locals, "noCctor");
                int variablesReference_StaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference_noCctor, "Static members");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "s_int", "5");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vs_int", "6");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "s_nint", "7");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "s_dec", "8");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "s_string", "\"test\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "s_array", "{int[2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "s_array2", "{int[2, 2, 2]}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "s_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "s_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "s_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "s_class", "{TestThreadStatic.TestStaticClass}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "s_class2", "{TestThreadStatic.TestStaticClass2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "s_class3", "{TestThreadStatic.ParentClass.TestStaticClass3}");

                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "t_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int", "vt_int", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "nint", "t_nint", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "decimal", "t_dec", "0");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "string", "t_string", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[]", "t_array", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "int[,,]", "t_array2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct", "t_struct", "{TestThreadStatic.TestStaticStruct}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticStruct2", "t_struct2", "{TestThreadStatic.TestStaticStruct2}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticStruct3", "t_struct3", "{TestThreadStatic.ParentClass.TestStaticStruct3}");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass", "t_class", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.TestStaticClass2", "t_class2", "null");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference_StaticReference, "TestThreadStatic.ParentClass.TestStaticClass3", "t_class3", "null");

                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}
}
