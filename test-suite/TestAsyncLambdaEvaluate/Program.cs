using System;
using System.IO;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestAsyncLambdaEvaluate
{
class TestWithThis
{
    delegate void Lambda1(string argVar);
    delegate void Lambda2(int i);
    delegate void Lambda3();

    int field_i = 33;

    public static int test_funct1()
    {
        return 1;
    }

    public int test_funct2()
    {
        return 2;
    }

    public void FuncLambda(int arg_i)
    {
        int local_i = 10;                                                                      Label.Breakpoint("bp12");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp13");
        }

        String mainVar = "mainVar";
        Lambda1 lambda1 = (argVar) =>
        {
            string localVar = "localVar";                                                      Label.Breakpoint("bp14");
            Console.WriteLine(arg_i.ToString() + argVar + mainVar + localVar + field_i.ToString());
        };
        lambda1("argVar");

        Lambda2 lambda2 = (i) =>
        {
            test_funct1();                                                                     Label.Breakpoint("bp15");
            test_funct2();
        };
        lambda2(5);

        Lambda3 lambda3 = () =>
        {
            Console.WriteLine("none");                                                         Label.Breakpoint("bp16");
        };
        lambda3();

        Label.Checkpoint(
            "func_lambda", "func_async",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp12");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp12");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda2", "lambda2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda3", "lambda3");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp13");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp13");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda2", "lambda2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda3", "lambda3");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp14");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp14");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp15");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp15");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp16");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp16");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "field_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "arg_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    public async Task FuncAsync(int arg_i)
    {
        await Task.Delay(500);

        int local_i = 10;                                                                      Label.Breakpoint("bp17");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp18");
        }

        Label.Checkpoint(
            "func_async", "func_async_with_lambda",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp17");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp17");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "50", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp18");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp18");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "50", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    public async Task FuncAsyncWithLambda(int arg_i)
    {
        await Task.Delay(500);

        String mainVar = "mainVar";
        Lambda1 lambda1 = (argVar) =>
        {
            string localVar = "localVar";                                                      Label.Breakpoint("bp19");
            Console.WriteLine(argVar + mainVar + localVar);

            int mainVar2 = 5;
            Lambda2 lambda2 = (i) =>
            {
                string localVar2 = "localVar";                                                 Label.Breakpoint("bp20");
                Console.WriteLine(arg_i.ToString() + argVar + mainVar + localVar + mainVar2.ToString() + i.ToString() + localVar2 + field_i.ToString());
            };
            lambda2(5);

            Lambda3 lambda3 = () =>
            {
                test_funct1();                                                                 Label.Breakpoint("bp21");
                test_funct2();
            };
            lambda3();

            Lambda3 lambda4 = () =>
            {
                Console.WriteLine("none");                                                     Label.Breakpoint("bp22");
            };
            lambda4();
        };
        lambda1("argVar");

        int local_i = 10;                                                                      Label.Breakpoint("bp23");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp24");
        }

        Label.Checkpoint(
            "func_async_with_lambda", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp19");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp19");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda2", "lambda2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda3", "lambda3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.TestWithThis.Lambda3", "lambda4");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "mainVar2");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp20");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp20");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"localVar\"", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "mainVar2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda4", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp21");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda4", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp22");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "field_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "arg_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda4", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp23");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp23");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis.Lambda1}", "TestAsyncLambdaEvaluate.TestWithThis.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda4", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp24");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp24");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis}", "TestAsyncLambdaEvaluate.TestWithThis", "this");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "33", "int", "field_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.TestWithThis.Lambda1}", "TestAsyncLambdaEvaluate.TestWithThis.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda4", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2", "int", "test_funct2()");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }
}

class Program
{
    delegate void Lambda1(string argVar);
    delegate void Lambda2(int i);
    delegate void Lambda3();

    public static int test_funct1()
    {
        return 1;
    }

    public int test_funct2()
    {
        return 2;
    }

    static void FuncLambda(int arg_i)
    {
        int local_i = 10;                                                                      Label.Breakpoint("bp1");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp2");
        }

        String mainVar = "mainVar";
        Lambda1 lambda1 = (argVar) =>
        {
            string localVar = "localVar";                                                      Label.Breakpoint("bp3");
            Console.WriteLine(arg_i.ToString() + argVar + mainVar + localVar);
        };
        lambda1("argVar");

        Lambda3 lambda3 = () =>
        {
            Console.WriteLine("none");                                                         Label.Breakpoint("bp4");
        };
        lambda3();

        Label.Checkpoint("static_func_lambda", "static_func_async",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda3", "lambda3");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda3", "lambda3");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp4");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp4");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "arg_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static async Task FuncAsync(int arg_i)
    {
        await Task.Delay(500);

        int local_i = 10;                                                                      Label.Breakpoint("bp5");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp6");
        }

        Label.Checkpoint("static_func_async", "static_func_async_with_lambda",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp5");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "50", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp6");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp6");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "50", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static async Task FuncAsyncWithLambda(int arg_i)
    {
        await Task.Delay(500);

        String mainVar = "mainVar";
        Lambda1 lambda1 = (argVar) =>
        {
            string localVar = "localVar";                                                      Label.Breakpoint("bp7");
            Console.WriteLine(argVar + mainVar + localVar);

            int mainVar2 = 5;
            Lambda2 lambda2 = (i) =>
            {
                string localVar2 = "localVar";                                                 Label.Breakpoint("bp8");
                Console.WriteLine(arg_i.ToString() + argVar + mainVar + localVar + mainVar2.ToString() + i.ToString() + localVar2);
            };
            lambda2(5);

            Lambda3 lambda3 = () =>
            {
                Console.WriteLine("none");                                                     Label.Breakpoint("bp9");
            };
            lambda3();
        };
        lambda1("argVar");

        int local_i = 10;                                                                      Label.Breakpoint("bp10");

        {
            int scope_i = 20;                                                                  Label.Breakpoint("bp11");
        }

        Label.Checkpoint(
            "static_func_async_with_lambda", "func_lambda",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp7");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp7");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda2", "lambda2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestAsyncLambdaEvaluate.Program.Lambda3", "lambda3");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "mainVar2");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp8");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp8");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "string", "localVar2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argVar\"", "string", "argVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"localVar\"", "string", "localVar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "mainVar2");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp9");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp9");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "arg_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "local_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda1", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp10");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp10");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.Program.Lambda1}", "TestAsyncLambdaEvaluate.Program.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "scope_i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp11");
                frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp11");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "100", "int", "arg_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestAsyncLambdaEvaluate.Program.Lambda1}", "TestAsyncLambdaEvaluate.Program.Lambda1", "lambda1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "local_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0", "int", "scope_i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"mainVar\"", "string", "mainVar");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "i", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "argVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "localVar", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "mainVar2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda2", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lambda3", "error");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "this", "error");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "test_funct1()");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "test_funct2()", "error");
                Context.Continue(@"__FILE__:__LINE__");
            });
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "static_func_lambda",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(false, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp5");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp6");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp7");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp8");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp9");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp10");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp11");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp12");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp13");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp14");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp15");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp16");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp17");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp18");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp19");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp20");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp21");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp22");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp23");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp24");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        FuncLambda(10);
        FuncAsync(50).Wait();
        FuncAsyncWithLambda(100).Wait();

        TestWithThis testWithThis = new TestWithThis();
        testWithThis.FuncLambda(10);
        testWithThis.FuncAsync(50).Wait();
        testWithThis.FuncAsyncWithLambda(100).Wait();

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
