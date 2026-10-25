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
        Label.Checkpoint("init", "test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        throw new System.InvalidOperationException();                                             Label.Breakpoint("point1");

        Label.Checkpoint("test1", "test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "unhandled", "System.InvalidOperationException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: null);
            });

        Label.Checkpoint("test2", "test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterUnhandledWithOptions("!System.InvalidOperationException");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: null);
            });

        Label.Checkpoint("test3", "test4",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterUnhandledWithOptions("!System.Exception");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "unhandled", "System.InvalidOperationException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: null);
            });

        Label.Checkpoint("test4", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAll();
                Context.AddExceptionBreakpointFilterUnhandledWithOptions("!*");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", JMC: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.InvalidOperationException");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__", CheckExitCode: null);
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
