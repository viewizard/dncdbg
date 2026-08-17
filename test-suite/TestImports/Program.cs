using System;
using System.Collections.Generic;

using Sys = System;
using GenericCollections = System.Collections.Generic;

using UserInt32 = System.Int32;
using UserInt64 = System.Int64;
using UserSingle = System.Single;
using UserGuid = System.Guid;
using UserList1 = System.Collections.Generic.List<int>;
using UserList2 = System.Collections.Generic.List<System.Collections.Generic.List<System.Collections.Generic.Dictionary<int, string>>>;

using static System.Int32;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestImports
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "imports_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("imports_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                // Test ImportNamespace.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2147483647", "int", "Int32.MaxValue");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3.1415927", "float", "Single.Pi");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "Int32.Abs(-10)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "20", "int", "Int32.Max(10, 20)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-1", "int", "Int64.Sign(-50L)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "float", "Single.Floor(1.34F)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{2a4ecdc1-6b94-410f-9823-a04cb8093363}", "System.Guid",
                                                                        "new Guid(\"2a4ecdc1-6b94-410f-9823-a04cb8093363\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 0", "System.Collections.Generic.List<int>",
                                                                        "new List<int>()");

                // Test AliasNamespace.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2147483647", "int", "Sys.Int32.MaxValue");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3.1415927", "float", "Sys.Single.Pi");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "Sys.Int32.Abs(-10)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "20", "int", "Sys.Int32.Max(10, 20)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-1", "int", "Sys.Int64.Sign(-50L)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "float", "Sys.Single.Floor(1.34F)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{2a4ecdc1-6b94-410f-9823-a04cb8093363}", "System.Guid",
                                                                        "new Sys.Guid(\"2a4ecdc1-6b94-410f-9823-a04cb8093363\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 0", "System.Collections.Generic.List<int>",
                                                                        "new GenericCollections.List<int>()");

                // Test AliasType.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2147483647", "int", "UserInt32.MaxValue");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3.1415927", "float", "UserSingle.Pi");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "UserInt32.Abs(-10)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "20", "int", "UserInt32.Max(10, 20)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-1", "int", "UserInt64.Sign(-50L)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "float", "UserSingle.Floor(1.34F)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{2a4ecdc1-6b94-410f-9823-a04cb8093363}", "System.Guid",
                                                                        "new UserGuid(\"2a4ecdc1-6b94-410f-9823-a04cb8093363\")");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 0", "System.Collections.Generic.List<int>",
                                                                        "new UserList1()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 0",
                    "System.Collections.Generic.List<System.Collections.Generic.List<System.Collections.Generic.Dictionary<int, string>>>",
                    "new UserList2()");

                // Test ImportType.
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2147483647", "int", "MaxValue");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"2147483647\"", "string", "MaxValue.ToString()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "Abs(-10)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"10\"", "string", "Abs(-10).ToString()");

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
