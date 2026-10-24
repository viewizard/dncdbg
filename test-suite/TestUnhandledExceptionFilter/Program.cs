using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestUnhandledExceptionFilter
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

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAll();
                Context.AddExceptionBreakpointFilterAllWithOptions("!System.InvalidOperationException");
                Context.AddExceptionBreakpointFilterUnhandledWithOptions("!System.InvalidOperationException");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        throw new System.InvalidOperationException();                                             Label.Breakpoint("point1");


        Label.Checkpoint("test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                //Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                // At this point debugger stops at unhandled exception, no reason continue process, abort execution.
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: null);
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
