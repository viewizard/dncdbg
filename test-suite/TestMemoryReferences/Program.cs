using System;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestMemoryReferences
{
class TestClass1
{
    static int i = 5;
}

class Program
{
    static int TestMethod()
    {
        return 10;
    }

    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "reference_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__", supportsMemoryReferences : true);
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = 1;
        float f = 1.0f;
        decimal d = 1.0M;
        TestClass1 testClass1null;
        TestClass1 testClass1 = new TestClass1();
        List<int> list1 = new List<int>(5) {10, 20, 30, 40, 50};
        Dictionary<string, int> dictionary1 = new Dictionary<string, int>(){ { "Alice", 25 }, { "Bob", 30 } };

        ;                                                             Label.Breakpoint("BREAK1");

        Label.Checkpoint("reference_test", "no_reference_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-1", "int", "-i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "TestMethod()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "float", "f");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1.0", "decimal", "d");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestMemoryReferences.TestClass1", "testClass1null");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestMemoryReferences.TestClass1}", "TestMemoryReferences.TestClass1", "testClass1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "TestClass1.i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 5", "System.Collections.Generic.List<int>", "list1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 2", "System.Collections.Generic.Dictionary<string, int>", "dictionary1");

                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "i", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "-i", nullReference : true);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "1", nullReference : true);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "TestMethod()", nullReference : true);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "f", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "d", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "testClass1null", nullReference : true);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "testClass1", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "TestClass1.i", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "list1", nullReference : false);
                Context.CheckMemoryReference(@"__FILE__:__LINE__", frameId, "dictionary1", nullReference : false);

                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("no_reference_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__", supportsMemoryReferences : false);
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-1", "int", "-i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "int", "1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "TestMethod()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "float", "f");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1.0", "decimal", "d");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "null", "TestMemoryReferences.TestClass1", "testClass1null");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "{TestMemoryReferences.TestClass1}", "TestMemoryReferences.TestClass1", "testClass1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5", "int", "TestClass1.i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 5", "System.Collections.Generic.List<int>", "list1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "Count = 2", "System.Collections.Generic.Dictionary<string, int>", "dictionary1");

                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "i");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "-i");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "1");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "TestMethod()");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "f");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "d");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "testClass1null");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "testClass1");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "TestClass1.i");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "list1");
                Context.CheckNoMemoryReference(@"__FILE__:__LINE__", frameId, "dictionary1");

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
