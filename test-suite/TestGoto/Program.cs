using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestGoto
{
public class ConstrTest
{
    public int i = 5; // breakpoint target line
    public int j = 5; // goto target line

    public ConstrTest()
    {
        int i = 5;
    }

    public ConstrTest(int x)
    {
        int j = 5;
    }

    public ConstrTest(int i, int j)
    {
        int p = 5; // error goto target line
    }
}

class Program
{
    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "goto_test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddManualBreakpointAndAddID(@"__FILE__:__LINE__", "Program.cs", line: 12); // line number with "public int i = 5;" code
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // error goto target line
        int j = 5; // error goto target line
        int i = 5; i++; i--;
        ;                                                                 Label.Breakpoint("BREAK1");

        Label.Checkpoint("goto_test1", "goto_test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 53, column: 4, expectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", targetID: 1, expectedLine: 53, expectedColumn: 9);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 53, column: 21, expectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", targetID: 2, expectedLine: 53, expectedColumn: 20);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 53, column: 25, expectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", targetID: 3, expectedLine: 53, expectedColumn: 25);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 53, column: 30);
                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 52, column: 20);
                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 51, column: 4);

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 26, column: 5, expectedCount: 1);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", targetID: 4);

                Context.Continue(@"__FILE__:__LINE__");
            });

        ConstrTest cTest = new ConstrTest();

        Label.Checkpoint("goto_test2", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasManualBreakpointHit(@"__FILE__:__LINE__", "Program.cs", line: 12); // line number with "public int i = 5;" code

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", line: 12, column: 4, expectedCount: 3);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", targetID: 7);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", targetID: 6);
                Context.Goto(@"__FILE__:__LINE__", targetID: 5, expectedLine: 12, expectedColumn: 5);

                Context.Continue(@"__FILE__:__LINE__");
            });

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
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
