using System;
using System.IO;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestFormatSpecifiers
{

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testspecifiers",
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

        string testString = "test string";
        sbyte testSByte = -2;
        byte testByte = 1;
        short testShort = -3;
        ushort testUShort = 4;
        int testInt = -5;
        uint testUInt = 6;
        long testLong = -7;
        ulong testULong = 8;
        float testFloat = 9f;
        double testDouble = 10;
        decimal testDecimal = 11M;
        char testChar = 'ㅎ';

        int i = 1;                                                Label.Breakpoint("bp1");

        Label.Checkpoint("testspecifiers", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "bp1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"test string\"", "string", "testString");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "test string", "string", "testString,nq");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-2", "sbyte", "testSByte");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfe", "sbyte", "testSByte,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "1", "byte", "testByte");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x01", "byte", "testByte,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3", "short", "testShort");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffd", "short", "testShort,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "4", "ushort", "testUShort");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x0004", "ushort", "testUShort,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-5", "int", "testInt");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffffffb", "int", "testInt,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "6", "uint", "testUInt");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x00000006", "uint", "testUInt,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-7", "long", "testLong");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0xfffffffffffffff9", "long", "testLong,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "8", "ulong", "testULong");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x0000000000000008", "ulong", "testULong,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "9", "float", "testFloat");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "9", "float", "testFloat,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "double", "testDouble");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "double", "testDouble,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "decimal", "testDecimal");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "decimal", "testDecimal,h");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "12622 'ㅎ'", "char", "testChar");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "0x314e 'ㅎ'", "char", "testChar,h");

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
