using System;
using System.IO;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestSrcBreakpointResolve
{
class test_constructors
{
    int test_field = 5; // bp here! make sure you correct code (test constructor)!

    public test_constructors()
    {
        int i = 5; // bp here! make sure you correct code (test constructor)!
    }

    public test_constructors(int i)
    {
        int j = 5;
    }
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "bp_test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");

                // setup breakpoints before process start
                // in this way we will check breakpoint resolve routine during module load

                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp0_delete_test1");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp0_delete_test2");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp1");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp2", "../Program.cs");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp3", "TestSrcBreakpointResolve/Program.cs");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp4", "./TestSrcBreakpointResolve/folder/../Program.cs");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp0_delete_test1");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");

                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp0_delete_test2");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

Label.Breakpoint("bp0_delete_test1");
Label.Breakpoint("bp0_delete_test2");
Label.Breakpoint("bp1");
Label.Breakpoint("bp2");
Label.Breakpoint("bp3");
Label.Breakpoint("resolved_bp1");       Console.WriteLine(
                                                          "Hello World!");          Label.Breakpoint("bp4");

        Label.Checkpoint(
            "bp_test1", "bp_test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp1", false);

                // check, that we have proper breakpoint ids
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp0_delete_test1"); // previously was deleted with id1
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                int? id7 = Context.GetBreakpointId(@"__FILE__:__LINE__", "bp0_delete_test1");
                Assert.Equal(Context.CurrentBpId, id7, @"__FILE__:__LINE__");
                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp0_delete_test1");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp5");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp5_resolve_wrong_source", "../wrong_folder/./Program.cs");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                int? id_bp5_b = Context.GetBreakpointId(@"__FILE__:__LINE__", "bp5_resolve_wrong_source", "../wrong_folder/./Program.cs");
                Assert.Equal(Context.CurrentBpId, id_bp5_b, @"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

Label.Breakpoint("bp5_resolve_wrong_source"); // Console.WriteLine("Hello World!");
                                              /* Console.WriteLine("Hello World!"); */
                                              Console.WriteLine("Hello World!");

Label.Breakpoint("bp5");                // Console.WriteLine("Hello World!");
                                        /* Console.WriteLine("Hello World!"); */
Label.Breakpoint("resolved_bp2");       Console.WriteLine("Hello World!");

        Label.Checkpoint(
            "bp_test2", "bp_test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp2", false);

                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp5_resolve_wrong_source", "../wrong_folder/./Program.cs");
                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp5");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp6", "./TestSrcBreakpointResolve/PROGRAM.CS");
                else
                    Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp6", "./TestSrcBreakpointResolve/Program.cs");

                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp6_resolve_wrong_source", "./wrong_folder/Program.cs");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                int? id_bp6_b = Context.GetBreakpointId(@"__FILE__:__LINE__", "bp6_resolve_wrong_source", "./wrong_folder/Program.cs");
                Assert.Equal(Context.CurrentBpId, id_bp6_b, @"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

                                        Console.WriteLine(
                                                          "Hello World!");          Label.Breakpoint("bp6_resolve_wrong_source");
Label.Breakpoint("resolved_bp3");       Console.WriteLine(
                                                          "Hello World!");          Label.Breakpoint("bp6");

        Label.Checkpoint(
            "bp_test3", "bp_test4",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp3", false);

                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp6", "./TestSrcBreakpointResolve/PROGRAM.CS");
                else
                    Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp6", "./TestSrcBreakpointResolve/Program.cs");

                Context.RemoveBreakpointAndRemoveID(@"__FILE__:__LINE__", "bp6_resolve_wrong_source", "./wrong_folder/Program.cs");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");

                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "resolved_bp4");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp7", "Program.cs");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp8", "TestSrcBreakpointResolve/Program.cs");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp9", "./TestSrcBreakpointResolve/folder/../Program.cs");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                int? current_bp_id = Context.GetBreakpointId(@"__FILE__:__LINE__", "bp9", "./TestSrcBreakpointResolve/folder/../Program.cs");
                // one more check, that we have proper breakpoint ids
                Assert.Equal(Context.CurrentBpId, current_bp_id, @"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

Label.Breakpoint("bp7");
Label.Breakpoint("bp8");
Label.Breakpoint("resolved_bp4");       Console.WriteLine(
                                                          "Hello World!");          Label.Breakpoint("bp9");

        Label.Checkpoint("bp_test4", "bp_test_nested",
            (Object context) =>
            {
                Context Context = (Context)context;
                // check, that actually we have only one active breakpoint per line
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resolved_bp4", false);

                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp10");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp11");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp12");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp13");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp14");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp15");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp16");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp17");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp18");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp19");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp20");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp20_1");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp20_2");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp21");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp21_1");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp21_2");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp22");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp22_1");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp22_2");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp23");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp24");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp25");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        TestSrcBreakpointResolve2.Program.testfunc();

        // tests resolve for nested methods
                                                                                Label.Breakpoint("bp10");
        void nested_func1()
        {                                                                       Label.Breakpoint("resloved_bp10");
            Console.WriteLine("Hello World!");
                                                                                Label.Breakpoint("bp11");
        }                                                                       Label.Breakpoint("resloved_bp11");
        nested_func1();
                                                                                Label.Breakpoint("bp12");
        void nested_func2()
        {                                                                       Label.Breakpoint("resloved_bp12");
            Console.WriteLine("Hello World!");                                  Label.Breakpoint("bp13");
        }
        nested_func2();
                                                                                Label.Breakpoint("bp14");
        Console.WriteLine("Hello World!");                                      Label.Breakpoint("resloved_bp14");

        void nested_func3()
        {
            Console.WriteLine("Hello World!");                                  Label.Breakpoint("bp15");
        }
        nested_func3();

        void nested_func4() { }; void nested_func5() { };                       Label.Breakpoint("bp16");
        nested_func4();

        void nested_func6() {
        }; void nested_func7() { };                                             Label.Breakpoint("bp17");
        nested_func6();

        void nested_func8() { }; void nested_func9() {
        }; void nested_func10() { };                                            Label.Breakpoint("bp18");
        nested_func9();

        void nested_func11() { void nested_func12() { void nested_func13() { 
                                                                                Label.Breakpoint("bp19");
        };                                                                      Label.Breakpoint("resloved_bp19");
        nested_func13(); }; 
        nested_func12(); };
        nested_func11();

        Console.WriteLine("1111Hello World!"); void nested_func14() {           Label.Breakpoint("bp20");
        Console.WriteLine("2222Hello World!");
        };                                                                      Label.Breakpoint("bp22");
        nested_func14();                                                        Label.Breakpoint("bp21");

        List<string> numbers = new List<string>();
        numbers.Add("1");
        numbers.Add("2");

Label.Breakpoint("bp20_1");            numbers.ForEach((string number) => {
            Console.WriteLine(number);                                          Label.Breakpoint("bp21_1");
        });                                                                     Label.Breakpoint("bp22_1");

Label.Breakpoint("bp20_2");            numbers.ForEach(delegate(string number) {
                                                                               Label.Breakpoint("bp21_2");
            Console.WriteLine(number);                                          Label.Breakpoint("bp21_2_resolved");
        });                                                                     Label.Breakpoint("bp22_2");

        Label.Checkpoint("bp_test_nested", "bp_test_constructor",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resloved_bp10", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resloved_bp11", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resloved_bp12", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp13", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resloved_bp14", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp15", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp16", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp17", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp18", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "resloved_bp19", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp20", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22", false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp20_1", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21_1", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22_1", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21_1", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22_1", false);
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp20_2", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21_2_resolved", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22_2", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp21_2_resolved", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp22_2", false);
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test constructor

        int bp = 23;                                                            Label.Breakpoint("bp23");
        test_constructors test_constr1 = new test_constructors();
        test_constructors test_constr2 = new test_constructors(5);

        Label.Checkpoint("bp_test_constructor", "bp_test_not_ordered_line_num",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp23", false);

                Context.AddManualBreakpointAndAddID(@"__FILE__:__LINE__", "Program.cs", 13); // line number with "int test_field = 5;" code
                Context.AddManualBreakpointAndAddID(@"__FILE__:__LINE__", "Program.cs", 17); // line number with "int i = 5;" code
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasManualBreakpointHit(@"__FILE__:__LINE__", "Program.cs", 13); // line number with "int test_field = 5;" code
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasManualBreakpointHit(@"__FILE__:__LINE__", "Program.cs", 17); // line number with "int i = 5;" code
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasManualBreakpointHit(@"__FILE__:__LINE__", "Program.cs", 13); // line number with "int test_field = 5;" code
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test code with sequence points that not ordered by line numbers

        Label.Breakpoint("bp24"); while(true)
        {
            break;                                                              Label.Breakpoint("bp25");
        }

        Label.Checkpoint("bp_test_not_ordered_line_num", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp24", false);
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp25", false);
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
