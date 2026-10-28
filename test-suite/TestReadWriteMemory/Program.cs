using System;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestReadWriteMemory
{
class Program
{
    static int Main(string[] args)
    {
        Label.Checkpoint("init", "rw_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__", supportsMemoryReferences : true);
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK2");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int i = -42443;
        int j = 345;
        ;                                                             Label.Breakpoint("BREAK1");

        Label.Checkpoint("rw_test", "eval_test",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-42443", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "345", "int", "j");

                string MemoryReference_i = Context.GetMemoryReference(@"__FILE__:__LINE__", frameId, "i")!;
                // Check `int i` data
                string DataBase64_i = Context.ReadMemory(@"__FILE__:__LINE__", MemoryReference_i, count: 4);
                byte[] decodedBytes_i = Convert.FromBase64String(DataBase64_i);
                int i = BitConverter.ToInt32(decodedBytes_i, 0);
                Assert.Equal(-42443, i, @"__FILE__:__LINE__");

                string MemoryReference_j = Context.GetMemoryReference(@"__FILE__:__LINE__", frameId, "j")!;
                // Check `int j` data
                string DataBase64_j = Context.ReadMemory(@"__FILE__:__LINE__", MemoryReference_j, count: 4);
                byte[] decodedBytes_j = Convert.FromBase64String(DataBase64_j);
                int j = BitConverter.ToInt32(decodedBytes_j, 0);
                Assert.Equal(345, j, @"__FILE__:__LINE__");

                // Swap i and j
                Context.WriteMemory(@"__FILE__:__LINE__", MemoryReference_i, DataBase64_j);
                Context.WriteMemory(@"__FILE__:__LINE__", MemoryReference_j, DataBase64_i);

                Context.Continue(@"__FILE__:__LINE__");
            });

        ;                                                             Label.Breakpoint("BREAK2");

        Label.Checkpoint("eval_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK2");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK2");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "345", "int", "i");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-42443", "int", "j");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // One more test for i and j current values
        if (j != -42443 || i != 345)
        {
            return 1;
        }

        return 0;

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
