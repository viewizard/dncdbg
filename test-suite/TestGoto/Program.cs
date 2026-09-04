using System;
using System.IO;

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
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.AddManualBreakpointAndAddID(@"__FILE__:__LINE__", "Program.cs", Line: 12); // line number with "public int i = 5;" code
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

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

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 54, Column: 4, ExpectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", TargetID: 1, ExpectedLine: 54, ExpectedColumn: 9);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 54, Column: 21, ExpectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", TargetID: 2, ExpectedLine: 54, ExpectedColumn: 20);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 54, Column: 25, ExpectedCount: 1);
                Context.Goto(@"__FILE__:__LINE__", TargetID: 3, ExpectedLine: 54, ExpectedColumn: 25);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");

                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 54, Column: 30);
                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 53, Column: 20);
                Context.CheckErrorGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 52, Column: 4);

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 27, Column: 5, ExpectedCount: 1);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", TargetID: 4);

                Context.Continue(@"__FILE__:__LINE__");
            });

        ConstrTest cTest = new ConstrTest();

        Label.Checkpoint("goto_test2", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasManualBreakpointHit(@"__FILE__:__LINE__", "Program.cs", Line: 12); // line number with "public int i = 5;" code

                Context.GetGotoTargets(@"__FILE__:__LINE__", "Program.cs", Line: 13, Column: 4, ExpectedCount: 3);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", TargetID: 5);
                Context.CheckErrorGoto(@"__FILE__:__LINE__", TargetID: 6);
                Context.Goto(@"__FILE__:__LINE__", TargetID: 7, ExpectedLine: 13, ExpectedColumn: 5);

                Context.Continue(@"__FILE__:__LINE__");
            });

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
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
