using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestObjectCreation
{
struct Coordinate(int X, int Y)
{
    public int X = X;
    public int Y = Y;
}

class Program
{
    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "bp_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int marker = 1;                                                    Label.Breakpoint("BREAK1");

        Label.Checkpoint("bp_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                // Plain constructor calls -- BCL value type, fully qualified.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId,
                    "{2a4ecdc1-6b94-410f-9823-a04cb8093363}", "System.Guid",
                    "new System.Guid(\"2a4ecdc1-6b94-410f-9823-a04cb8093363\")");

                // TODO:
                // Same, but bare/unqualified ("Guid" instead of "System.Guid").
                // MetadataHelpers::FindType has no notion of the evaluated
                // expression's `using` directives (unlike Roslyn's real
                // binder), so this needs FindTypeByShortNameInModule's
                // short-name fallback: search every loaded module's
                // top-level types for one whose name (ignoring namespace)
                // matches, tried only once the normal namespace-qualified
                // lookup fails for a single wholly-unqualified identifier.
                // Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId,
                //     "{2a4ecdc1-6b94-410f-9823-a04cb8093363}", "System.Guid",
                //     "new Guid(\"2a4ecdc1-6b94-410f-9823-a04cb8093363\")");

                // User-defined struct, single identifier (no namespace needed).
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestObjectCreation.Coordinate}", "TestObjectCreation.Coordinate", "new Coordinate(3, 4)");

                // Generic BCL class -- constructor type parameters for the
                // type itself (not just the .ctor's own args, which are
                // none here) must be threaded through correctly.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId,
                    "Count = 0", "System.Collections.Generic.List<int>",
                    "new System.Collections.Generic.List<int>()");

                // Error paths: unknown type, no matching overload, object/
                // collection initializers (deliberately unimplemented --
                // see the object_creation_expression parser handler).
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "new NoSuchType(1)",
                    "error: The type or namespace name 'NoSuchType' couldn't be found");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "new System.Guid(1, 2, 3)",
                    "error: 'System.Guid' has no accessible constructor taking 3 arguments");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId,
                    "new System.Collections.Generic.List<int> { 1, 2, 3 }",
                    "Object/collection initializers are not implemented");

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
