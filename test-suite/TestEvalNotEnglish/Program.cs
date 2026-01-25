using System;
using System.IO;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestEvalNotEnglish
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "eval_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("영어 출력이 아닌 테스트.");
        Console.WriteLine("测试非英语输出。");

        int 당신 = 1;
        당신++;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("eval_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");

                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");
                Context.CalcAndCheckExpression(@"__FILE__:__LINE__", frameId, "1", "당신");
                Context.CalcAndCheckExpression(@"__FILE__:__LINE__", frameId, "12", "당신 + 11");

                Context.CalcExpressionWithNotDeclared(@"__FILE__:__LINE__", frameId, "你");
                Context.CalcExpressionWithNotDeclared(@"__FILE__:__LINE__", frameId, "你 + 1");

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
