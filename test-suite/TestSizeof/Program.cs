using System;
using System.IO;
using System.Collections.Generic;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace TestSizeof
{
public struct Point
{
    public Point(byte tag, decimal x, decimal y) => (Tag, X, Y) = (tag, x, y);

    public decimal X { get; }
    public decimal Y { get; }
    public byte Tag { get; }
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
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        int a = 10;
        TestStruct tc = new TestStruct(a, 11);
        string str1 = "string1";
        uint c;
        unsafe
        {
            c = (uint)sizeof(Point);
        }
        uint d = c;                                                   Label.Breakpoint("BREAK1");

        Label.Checkpoint("bp_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(bool).ToString(), "uint", "sizeof(bool)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(byte).ToString(), "uint", "sizeof(byte)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(sbyte).ToString(), "uint", "sizeof(sbyte)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(char).ToString(), "uint", "sizeof(char)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(int).ToString(), "uint", "sizeof(int)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(uint).ToString(), "uint", "sizeof(uint)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(long).ToString(), "uint", "sizeof(long)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(ulong).ToString(), "uint", "sizeof(ulong)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(float).ToString(), "uint", "sizeof(float)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(double).ToString(), "uint", "sizeof(double)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(decimal).ToString(), "uint", "sizeof(decimal)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Boolean).ToString(), "uint", "sizeof(System.Boolean)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Byte).ToString(), "uint", "sizeof(System.Byte)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Char).ToString(), "uint", "sizeof(System.Char)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Decimal).ToString(), "uint", "sizeof(System.Decimal)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Double).ToString(), "uint", "sizeof(System.Double)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Int16).ToString(), "uint", "sizeof(System.Int16)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Int32).ToString(), "uint", "sizeof(System.Int32)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Int64).ToString(), "uint", "sizeof(System.Int64)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.SByte).ToString(), "uint", "sizeof(System.SByte)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.Single).ToString(), "uint", "sizeof(System.Single)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.UInt16).ToString(), "uint", "sizeof(System.UInt16)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.UInt32).ToString(), "uint", "sizeof(System.UInt32)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, sizeof(System.UInt64).ToString(), "uint", "sizeof(System.UInt64)");
                string ss1;
                Context.GetResultAsString(@"__FILE__:__LINE__", frameId, "sizeof(Point)", out ss1);
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, ss1, "uint", "c");

                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(a)", "error:");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(tc)", "error:");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(str1)", "error:");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(abcd)", "error: The type or namespace name");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(Program)", "error:");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "sizeof(tc.a)", "");

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

    struct TestStruct
    {
        public int a;
        public int b;

        public TestStruct(int x, int y)
        {
            a = x;
            b = y;
        }
    }
}
}
