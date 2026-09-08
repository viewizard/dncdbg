using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestEmbeddedSources
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
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpointAndAddID(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpointsAndCheckIDs(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // The repeated lines below make this file large enough for Roslyn to
        // store its embedded source compressed in the PDB, while the tiny
        // Program2.cs is stored uncompressed. Both variants are checked via
        // "source" requests after the breakpoint is hit.
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");
        System.Console.WriteLine("Compressed source file");

        ;                                                                 Label.Breakpoint("bp1");

        Label.Checkpoint("sources_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1", CheckSourcePath: false);

                int sourceReference = Context.GetSourceReferenceFromTopFrame(@"__FILE__:__LINE__");

                Context.CompareSources(@"__FILE__:__LINE__", "Program.cs", SourceReference: sourceReference, SourcePath: "");
                Context.CompareSources(@"__FILE__:__LINE__", "Program2.cs", SourceReference: 0, SourcePath: "Program2.cs");

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
