using System;
using System.IO;
using System.Collections.Generic;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestGeneric
{
class Program
{
    public class MY
    {
        public int m;
        public static int retme(int arg1)
        {
            return arg1;
        }

        public int retm()
        {
            return m;
        }
    }

    public class CHILDOFMY<T> : MY
    {
        public T rett(T arg)
        {
            return arg;
        }

        public U retu<U>(U arg)
        {
            return arg;
        }
    }

    public class TestNested<X, Y>
    {
        public class Nested<A, B>
        {
            public static MY my;

            public A test1(A arga)
            {
                return arga;
            }

            public B test1(B argb)
            {
                return argb;
            }

            public C test3<C>(A arga, B argb, C argc)
            {
                return argc;
            }

            public static A static_test1(A arga)
            {
                return arga;
            }

            public static B static_test1(B argb)
            {
                return argb;
            }

            public static C static_test3<C>(A arga, B argb, C argc)
            {
                return argc;
            }
        }

        public static Nested<X, Y> static_nested;
        public Nested<X, Y> nested;
        public Nested<X, Y> uninitialized;
        public TestNested()
        {
            nested = new Nested<X, Y>();
        }
    }

    public class TestGeneric<T, U>
    {
        public T test1(T argt)
        {
            return argt;
        }

        public U test1(U argu)
        {
            return argu;
        }

        public T test12(T argt, U argu)
        {
            return argt;
        }

        public U test21(U argu, T argt)
        {
            return argu;
        }

        public W test2<W>(W argw)
        {
            return argw;
        }

        public W test3<W>(T argt, U argu, W argw)
        {
            return argw;
        }

        public W test41<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argw;
        }

        public Y test42<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argy;
        }

        public Z test43<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argz;
        }

        public T test44<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argt;
        }

        static public T static_test1(T argt)
        {
            return argt;
        }

        static public U static_test1(U argu)
        {
            return argu;
        }

        static public W static_test2<W>(W argw)
        {
            return argw;
        }

        static public W static_test3<W>(T argt, U argu, W argw)
        {
            return argw;
        }

        static public W static_test41<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argw;
        }

        static public Y static_test42<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argy;
        }

        static public Z static_test43<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argz;
        }

        static public T static_test44<W, Y, Z>(W argw, Y argy, Z argz, T argt)
        {
            return argt;
        }

        public T i1;
        public U s1;

        public T p_i1 { get; set; }

        public U p_s1 { get; set; }

        public static T static_i1;
        public static U static_s1;

        public static T static_p_i1 { get; set; }

        public static U static_p_s1 { get; set; }

        public void test_func()
        {
            MY my = new MY();
            CHILDOFMY<int> ichofmy = new CHILDOFMY<int>();
            CHILDOFMY<string> schofmy = new CHILDOFMY<string>();
            ichofmy.m = 15;
            schofmy.m = 15;

            Console.WriteLine("test_func()");                                                            Label.Breakpoint("BREAK2");

            Label.Checkpoint(
                "test_func", "test_set_value",
                (Object context) =>
                {
                    Context Context = (Context)context;
                    Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK2");
                    Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK2");

                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.TestGeneric<int, string>}", "TestGeneric.Program.TestGeneric<int, string>", "this");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "123", "int", "i1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string1\"", "string", "s1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "234", "int", "p_i1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string2\"", "string", "p_s1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "345", "int", "static_i1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string3\"", "string", "static_s1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "456", "int", "static_p_i1");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string4\"", "string", "static_p_s1");

                    // Instance methods
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "test1(5)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"five\"", "string", "test1(\"five\")");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test1(false)", "error");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "test12(5,\"five\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"five\"", "string", "test21(\"five\",5)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "test2<int>(10)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "false", "bool", "test2<bool>(false)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "test2<char>('a')");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "test2<string>(\"abc\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.MY}", "TestGeneric.Program.MY", "test2<MY>(my)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "test3<char>(101,\"string\",'a')");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test3<bool>(101,\"string\",'a')", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test3<char>(101,'a','a')", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test3<char>('a',\"string\",'a')", "error");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "test41<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "test42<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"string\"", "string", "test43<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "41", "int", "test44<char,bool,string>('a',true,\"string\", 41)");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test44<bool,string,char>('a',true,\"string\", 41)", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test44<string,char,bool>('a',true,\"string\", 41)", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test44<bool,char,string>('a',true,\"string\", 41)", "error");

                    // Static methods

                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "static_test1(15)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"fifteen\"", "string", "static_test1(\"fifteen\")");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test1(false)", "error");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "20", "int", "static_test2<int>(20)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "static_test2<bool>(true)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "120 'x'", "char", "static_test2<char>('x')");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"xyz\"", "string", "static_test2<string>(\"xyz\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.MY}", "TestGeneric.Program.MY", "static_test2<MY>(my)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "static_test3<char>(101,\"string\",'a')");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test3<bool>(101,\"string\",'a')", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test3<char>(101,'a','a')", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test3<char>('a',\"string\",'a')", "error");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "static_test41<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "static_test42<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"string\"", "string", "static_test43<char,bool,string>('a',true,\"string\", 41)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "41", "int", "static_test44<char,bool,string>('a',true,\"string\", 41)");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test44<bool,string,char>('a',true,\"string\", 41)", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test44<string,char,bool>('a',true,\"string\", 41)", "error");
                    Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "static_test44<bool,char,string>('a',true,\"string\", 41)", "error");

                    // Inherited methods
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.rett(15)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.retu<int>(15)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "ichofmy.retu<char>('a')");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "ichofmy.retu<string>(\"abc\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.retm()");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"TestGeneric.Program+CHILDOFMY`1[System.Int32]\"", "string", "ichofmy.ToString()");

                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "schofmy.rett(\"abc\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "schofmy.retu<int>(15)");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "schofmy.retu<char>('a')");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "schofmy.retu<string>(\"abc\")");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "schofmy.retm()");
                    Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"TestGeneric.Program+CHILDOFMY`1[System.String]\"", "string", "schofmy.ToString()");

                    // Test set value.

                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "i1", "55");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "s1", "\"changed1\"");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "p_i1", "66");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "p_s1", "\"changed2\"");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "static_i1", "77");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "static_s1", "\"changed3\"");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "static_p_i1", "88");
                    Context.SetExpression(@"__FILE__:__LINE__", frameId, "static_p_s1", "\"changed4\"");

                    int variablesReference = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                    int setReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference, "this");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "i1", "55555");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "s1", "\"changed_string111\"");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "p_i1", "66666");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "p_s1", "\"changed_string222\"");

                    int setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", setReference, "Static members");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_i1", "77777");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_s1", "\"changed_string333\"");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_p_i1", "88888");
                    Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_p_s1", "\"changed_string444\"");

                    Context.Continue(@"__FILE__:__LINE__");
                });
        }
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_main",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK3");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        TestGeneric<int, string> ttt = new TestGeneric<int, string>();
        ttt.i1 = 123;
        ttt.s1 = "test_string1";
        ttt.p_i1 = 234;
        ttt.p_s1 = "test_string2";
        TestGeneric<int, string>.static_i1 = 345;
        TestGeneric<int, string>.static_s1 = "test_string3";
        TestGeneric<int, string>.static_p_i1 = 456;
        TestGeneric<int, string>.static_p_s1 = "test_string4";
        MY my = new MY();
        CHILDOFMY<int> ichofmy = new CHILDOFMY<int>();
        CHILDOFMY<string> schofmy = new CHILDOFMY<string>();
        ichofmy.m = 15;
        schofmy.m = 15;
        TestNested<char, int> testnested = new TestNested<char, int>();
        TestNested<char, int> uninitialized;

        ttt.test_func();                                                                                 Label.Breakpoint("BREAK1");

        Label.Checkpoint(
            "test_main", "test_func",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.TestGeneric<int, string>}", "TestGeneric.Program.TestGeneric<int, string>", "ttt");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "123", "int", "ttt.i1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string1\"", "string", "ttt.s1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "234", "int", "ttt.p_i1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string2\"", "string", "ttt.p_s1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.static_i1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.static_s1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.static_p_i1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.static_p_s1", "error");

                // Static members
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "345", "int", "TestGeneric<int, string>.static_i1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string3\"", "string", "TestGeneric<int, string>.static_s1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "456", "int", "TestGeneric<int, string>.static_p_i1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test_string4\"", "string", "TestGeneric<int, string>.static_p_s1");

                // Instance methods
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "ttt.test1(5)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"five\"", "string", "ttt.test1(\"five\")");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test1(false)", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "ttt.test12(5,\"five\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"five\"", "string", "ttt.test21(\"five\",5)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "ttt.test2<int>(10)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "false", "bool", "ttt.test2<bool>(false)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "ttt.test2<char>('a')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "ttt.test2<string>(\"abc\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.MY}", "TestGeneric.Program.MY", "ttt.test2<MY>(my)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "ttt.test3<char>(101,\"string\",'a')");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test3<bool>(101,\"string\",'a')", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test3<char>(101,'a','a')", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test3<char>('a',\"string\",'a')", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "ttt.test41<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "ttt.test42<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"string\"", "string", "ttt.test43<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "41", "int", "ttt.test44<char,bool,string>('a',true,\"string\", 41)");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test44<bool,string,char>('a',true,\"string\", 41)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test44<string,char,bool>('a',true,\"string\", 41)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "ttt.test44<bool,char,string>('a',true,\"string\", 41)", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "testnested.nested.test1('a')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "123", "int", "testnested.nested.test1(123)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "789", "int", "testnested.nested.test3<int>('c',456,789)");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "testnested.nested.test3<int>(false,456,789)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "testnested.nested.test3<int>('c',456,\"abc\")", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"cde\"", "string", "testnested.nested.test3<string>('c',456,\"cde\")");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "testnested.nested.test3<string>(false,456,\"cde\")", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "testnested.nested.test3<string>('c',456,789)", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestGeneric.Program.TestNested<char, int>", "uninitialized?.uninitialized?.test3<string>('c',456,\"cde\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestGeneric.Program.TestNested<char, int>.Nested<char, int>", "testnested.uninitialized?.test3<string>('c',456,\"cde\")");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "uninitialized.uninitialized?.test3<string>('c',456,\"cde\")", "error");

                // Static methods
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "TestGeneric<int,string>.static_test1(15)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"fifteen\"", "string", "TestGeneric<int,string>.static_test1(\"fifteen\")");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test1(false)", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "20", "int", "TestGeneric<int,string>.static_test2<int>(20)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "TestGeneric<int,string>.static_test2<bool>(true)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "120 'x'", "char", "TestGeneric<int,string>.static_test2<char>('x')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"xyz\"", "string", "TestGeneric<int,string>.static_test2<string>(\"xyz\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestGeneric.Program.MY}", "TestGeneric.Program.MY", "TestGeneric<int,string>.static_test2<MY>(my)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "TestGeneric<int,string>.static_test3<char>(101,\"string\",'a')");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test3<bool>(101,\"string\",'a')", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test3<char>(101,'a','a')", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test3<char>('a',\"string\",'a')", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "TestGeneric<int,string>.static_test41<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "TestGeneric<int,string>.static_test42<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"string\"", "string", "TestGeneric<int,string>.static_test43<char,bool,string>('a',true,\"string\", 41)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "41", "int", "TestGeneric<int,string>.static_test44<char,bool,string>('a',true,\"string\", 41)");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test44<bool,string,char>('a',true,\"string\", 41)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test44<string,char,bool>('a',true,\"string\", 41)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_test44<bool,char,string>('a',true,\"string\", 41)", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "TestNested<int,string>.Nested<char,bool>.static_test1('a')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "true", "bool", "TestNested<int,string>.Nested<char,bool>.static_test1(true)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "TestNested<int,string>.Nested<char,bool>.static_test3<string>('a', true, \"abc\")");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestNested<int,string>.Nested<char,bool>.static_test1(\"xyz\")", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestNested<int,string>.Nested<char,bool>.static_test3<string>('a', true, 123)", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "TestNested<int,string>.Nested<char,bool>.static_test3<string>(123, true, \"abc\")", "error");

                // Inherited methods
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.rett(15)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.retu<int>(15)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "ichofmy.retu<char>('a')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "ichofmy.retu<string>(\"abc\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "ichofmy.retm()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"TestGeneric.Program+CHILDOFMY`1[System.Int32]\"", "string", "ichofmy.ToString()");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "schofmy.rett(\"abc\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "schofmy.retu<int>(15)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "97 'a'", "char", "schofmy.retu<char>('a')");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"abc\"", "string", "schofmy.retu<string>(\"abc\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "15", "int", "schofmy.retm()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"TestGeneric.Program+CHILDOFMY`1[System.String]\"", "string", "schofmy.ToString()");

                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("test set value");                                                             Label.Breakpoint("BREAK3");

        Label.Checkpoint(
            "test_set_value", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK3");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK3");

                Context.SetExpression(@"__FILE__:__LINE__", frameId, "ttt.i1", "555");
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "ttt.s1", "\"changed_string1\"");
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "ttt.p_i1", "666");
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "ttt.p_s1", "\"changed_string2\"");

                int variablesReference = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                int setReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", variablesReference, "ttt");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "i1", "5555");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "s1", "\"changed_string11\"");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "p_i1", "6666");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setReference, "p_s1", "\"changed_string22\"");

                int setStaticReference = Context.GetChildVariablesReference(@"__FILE__:__LINE__", setReference, "Static members");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_i1", "7777");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_s1", "\"changed_string33\"");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_p_i1", "8888");
                Context.SetVariable(@"__FILE__:__LINE__", frameId, setStaticReference, "static_p_s1", "\"changed_string44\"");

                // FIXME debugger must be fixed first
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_i1", "777");
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_s1", "\"changed_string3\"");

                // FIXME debugger must be fixed first
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_p_i1", "888");
                Context.SetExpression(@"__FILE__:__LINE__", frameId, "TestGeneric<int,string>.static_p_s1", "\"changed_string4\"");

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
