using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestStartAt0
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "bp_test_column",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__", linesStartAt1 : false, columnsStartAt1 : false);

                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp1", column: 2,  linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp2", column: 22, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp3", column: 26, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp4", column: 36, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", column: 2,  linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", column: 22, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", column: 26, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp6", column: 35, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp7", column: 50, linesStartAt1 : false, columnsStartAt1 : false);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp8", column: 50, linesStartAt1 : false, columnsStartAt1 : false);
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 0;
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("bp1");
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("bp2");
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("bp3");
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("bp4");
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("resolved_bp4");
        i = 5 + 5; --i; i++;                                                    Label.Breakpoint("bp5");

        nested_func1();

        Label.Breakpoint("bp6");            void nested_func1()
        {                                                                       Label.Breakpoint("resolved_bp6");
            Console.WriteLine("Hello World!");                                  Label.Breakpoint("bp7");
        }                                                                       Label.Breakpoint("resolved_bp7");

        nested_func2();                                                         Label.Breakpoint("bp8");
        void nested_func2()
        {                                                                       Label.Breakpoint("resolved_bp8");
            Console.WriteLine("Hello World!");
        }

        Label.Checkpoint("bp_test_column", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2", checkSourcePath: true, expectedColumn: 20, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3", checkSourcePath: true, expectedColumn: 25, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp4", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", checkSourcePath: true, expectedColumn: 20, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", checkSourcePath: true, expectedColumn: 25, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp6", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp7", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp8", checkSourcePath: true, expectedColumn: 9, linesStartAt1 : false, columnsStartAt1 : false);
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
