using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestEnv
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "env_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStartWithEnv(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        string EnvA = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT");
        string EnvB = Environment.GetEnvironmentVariable("ASPNETCORE_URLS");

        Console.WriteLine("Env A = " + EnvA + " Env B = " + EnvB); Label.Breakpoint("bp");

        Label.Checkpoint(
            "env_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp");

                int variablesReference_Locals = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                int variablesReference = Context.GetVariablesReference(@"__FILE__:__LINE__", frameId, "Locals");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference, "string", "EnvA", "\"Development\"");
                Context.EvalVariable(@"__FILE__:__LINE__", variablesReference, "string", "EnvB", "\"https://localhost:25001\"");

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
