using System;
using System.IO;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestBreakpointColumn
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "bp_test_column",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");

                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp1", 2);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp2", 22);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp3", 26);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp4", 36);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", 2);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", 22);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp5", 26);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp6", 35);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp7", 50);
                Context.AddBreakpointWithColumn(@"__FILE__:__LINE__", "bp8", 50);
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.ConfigurationDone(@"__FILE__:__LINE__");
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
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1", true, 9);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp2", true, 20);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp3", true, 25);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp4", true, 9);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", true, 9);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", true, 20);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp5", true, 25);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp6", true, 9);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp7", true, 9);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp8", true, 9);
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
