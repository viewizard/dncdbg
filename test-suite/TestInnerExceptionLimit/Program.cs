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
        Label.Checkpoint("init", "test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        var e = new InvalidOperationException("innermost");
        for (int i = 0; i < 64; ++i)
        {
            e = new InvalidOperationException("level " + i, e);
        }
        try { throw e; } catch (Exception) { Console.WriteLine("caught"); }  Label.Breakpoint("point1");

        Label.Checkpoint("test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.TestInnerExceptionMessages(@"__FILE__:__LINE__", ExpectedFirst: "level 62", ExpectedLast: "level 31");
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
