using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestExceptionWildcardCondition
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
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddExceptionBreakpointFilterAllWithConditions("*");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", jmc: true);

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        try { throw new System.Exception(); } catch (Exception) { ; }                              Label.Breakpoint("point1");

        Label.Checkpoint("test1", "test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point1", "CLR", "always", "System.Exception");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithConditions("System.*");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        try { throw new System.Exception(); } catch (Exception) { ; }                              Label.Breakpoint("point2");

        Label.Checkpoint("test2", "test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point2", "CLR", "always", "System.Exception");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithConditions("!System.Null*, *");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        try { throw new System.NullReferenceException(); } catch (Exception) { ; }
        try { throw new System.Exception(); } catch (Exception) { ; }                              Label.Breakpoint("point3");

        Label.Checkpoint("test3", "test4",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "point3", "CLR", "always", "System.Exception");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithConditions("System.Security.*, !System.*");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        try { throw new System.Security.SecurityException(); } catch (Exception) { ; }
        ;                                                                                          Label.Breakpoint("bp1");

        Label.Checkpoint("test4", "test5",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithConditions("System.*, !*");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        try { throw new System.Security.SecurityException(); } catch (Exception) { ; }
        ;                                                                                          Label.Breakpoint("bp2");

        Label.Checkpoint("test5", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2");
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
