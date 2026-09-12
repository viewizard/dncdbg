using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestBreakpointLocations
{
class Program
{
    static void TestMethod1()
    {
        int i = 0;
        i++; i--; i += 5;
    }

    static void TestMethod2()
    {
        int i = 0;
    }

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "locations_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("locations_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;

                BreakpointLocation[] ExpectedLocations1 = [ new (line: 13, column: 9, endLine:13, endColumn: 19) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations1,
                                                FileName: "Program.cs", Line: 13, Column: 0, EndLine: 0, EndColumn: 0);

                BreakpointLocation[] ExpectedLocations2 = [ new (line: 14, column: 9, endLine:14, endColumn: 13),
                                                            new (line: 14, column: 14, endLine:14, endColumn: 18),
                                                            new (line: 14, column: 19, endLine:14, endColumn: 26) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations2,
                                                FileName: "Program.cs", Line: 14, Column: 0, EndLine: 0, EndColumn: 0);

                BreakpointLocation[] ExpectedLocations3 = [ new (line: 14, column: 14, endLine:14, endColumn: 18),
                                                            new (line: 14, column: 19, endLine:14, endColumn: 26) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations3,
                                                FileName: "Program.cs", Line: 14, Column: 14, EndLine: 0, EndColumn: 0);

                BreakpointLocation[] ExpectedLocations4 = [ new (line: 14, column: 14, endLine:14, endColumn: 18),
                                                            new (line: 14, column: 19, endLine:14, endColumn: 26) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations4,
                                                FileName: "Program.cs", Line: 14, Column: 14, EndLine: 14, EndColumn: 0);

                BreakpointLocation[] ExpectedLocations5 = [ new (line: 14, column: 14, endLine:14, endColumn: 18) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations5,
                                                FileName: "Program.cs", Line: 14, Column: 14, EndLine: 14, EndColumn: 18);

                BreakpointLocation[] ExpectedLocations6 = [ new (line: 12, column: 5,  endLine:12, endColumn: 6),
                                                            new (line: 13, column: 9,  endLine:13, endColumn: 19),
                                                            new (line: 14, column: 9,  endLine:14, endColumn: 13),
                                                            new (line: 14, column: 14, endLine:14, endColumn: 18),
                                                            new (line: 14, column: 19, endLine:14, endColumn: 26),
                                                            new (line: 15, column: 5,  endLine:15, endColumn: 6) ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations6,
                                                FileName: "Program.cs", Line: 11, Column: 3, EndLine: 15, EndColumn: 18);

                BreakpointLocation[] ExpectedLocations7 = [ new (line: 12, column: 5,  endLine:12, endColumn: 6),
                                                            new (line: 13, column: 9,  endLine:13, endColumn: 19),
                                                            new (line: 14, column: 9,  endLine:14, endColumn: 13),
                                                            new (line: 14, column: 14, endLine:14, endColumn: 18),
                                                            new (line: 14, column: 19, endLine:14, endColumn: 26),
                                                            new (line: 15, column: 5,  endLine:15, endColumn: 6),
                                                            new (line: 18, column: 5,  endLine:18, endColumn: 6),
                                                            new (line: 19, column: 9,  endLine:19, endColumn: 19),
                                                            new (line: 20, column: 5,  endLine:20, endColumn: 6), ];
                Context.TestBreakpointLocations(@"__FILE__:__LINE__", ExpectedLocations7,
                                                FileName: "Program.cs", Line: 11, Column: 3, EndLine: 20, EndColumn: 18);

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
