using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestInnerExceptionLimit
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", jmc: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        var e = new InvalidOperationException("innermost");
        for (int i = 0; i < 64; ++i)
        {
            e = new InvalidOperationException("level " + i, e);
        }
        try { throw e; } catch (Exception) { Console.WriteLine("caught"); }  Label.Breakpoint("point1");

        Label.Checkpoint("test1", "test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.TestInnerExceptionMessages(@"__FILE__:__LINE__", expectedFirst: "level 62", expectedLast: "level 31");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("test2", "test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.AddEnvMapEntry("DNCDBG_INNER_EXCEPTION_WALK_LIMIT", "50");
                Context.Launch(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.TestInnerExceptionMessages(@"__FILE__:__LINE__", expectedFirst: "level 62", expectedLast: "level 13");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("test3", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.AddEnvMapEntry("DNCDBG_INNER_EXCEPTION_WALK_LIMIT", "100");
                Context.Launch(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.TestInnerExceptionMessages(@"__FILE__:__LINE__", expectedFirst: "level 62", expectedLast: "innermost");
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
