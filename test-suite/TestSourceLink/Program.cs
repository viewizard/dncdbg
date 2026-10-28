using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestSourceLink
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "sources_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        ;                                                                 Label.Breakpoint("bp1");

        Label.Checkpoint("sources_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1", checkSourcePath: false);

                int sourceReference = Context.GetSourceReferenceFromTopFrame(@"__FILE__:__LINE__");
                Context.CompareSources(@"__FILE__:__LINE__", "Program.cs", sourceReference: sourceReference, sourcePath: "");

                Context.CompareSources(@"__FILE__:__LINE__", "Program2.cs", sourceReference: 0, sourcePath: "Program2.cs");

                sourceReference = Context.GetSourceReferenceFromLoadedSources(@"__FILE__:__LINE__", sourceName: "Program.cs");
                Context.CompareSources(@"__FILE__:__LINE__", "Program.cs", sourceReference: sourceReference, sourcePath: "");

                sourceReference = Context.GetSourceReferenceFromLoadedSources(@"__FILE__:__LINE__", sourceName: "Program2.cs");
                Context.CompareSources(@"__FILE__:__LINE__", "Program2.cs", sourceReference: sourceReference, sourcePath: "");

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
