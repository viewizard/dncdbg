using System;
using System.IO;
using System.Collections.Generic;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestEvaluatePrimitiveUnary
{

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_unary",
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

        // Test all primitive types with all unary operators (+,-,~,!,++,--)

        bool bool1 = true;
        char char1 = 'G';
        string string1 = "test";

        byte byte1 = 35;
        ushort ushort1 = 5277;
        uint uint1 = 34256;
        ulong ulong1 = 45265345;

        sbyte sbyte1 = 42;
        short short1 = 3155;
        int int1 = 3549;
        long long1 = 2942426;

        sbyte sbyte1s = -42;
        short short1s = -3155;
        int int1s = -3549;
        long long1s = -2942426;

        double double1 = 235.432;
        float float1 = 678F;

        double double1s = -235.432;
        float float1s = -678F;

        int break_line1 = 1;                                              Label.Breakpoint("BREAK1");

        Label.Checkpoint("test_unary", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                // +
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "+bool1", "error: Operator '+' cannot be applied to operand of type 'bool'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "71", "int", "+char1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "+string1", "error: Operator '+' cannot be applied to operand of type 'string'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "35", "int", "+byte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "5277", "int", "+ushort1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "34256", "uint", "+uint1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "45265345", "ulong", "+ulong1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "42", "int", "+sbyte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3155", "int", "+short1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3549", "int", "+int1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2942426", "long", "+long1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-42", "int", "+sbyte1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3155", "int", "+short1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3549", "int", "+int1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-2942426", "long", "+long1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "235.432", "double", "+double1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "678", "float", "+float1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-235.432", "double", "+double1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-678", "float", "+float1s");

                // -
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "-bool1", "error: Operator '-' cannot be applied to operand of type 'bool'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-71", "int", "-char1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "-string1", "error: Operator '-' cannot be applied to operand of type 'string'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-35", "int", "-byte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-5277", "int", "-ushort1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-34256", "long", "-uint1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "-ulong1", "error: Operator '-' cannot be applied to operand of type 'ulong'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-42", "int", "-sbyte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3155", "int", "-short1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3549", "int", "-int1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-2942426", "long", "-long1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "42", "int", "-sbyte1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3155", "int", "-short1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3549", "int", "-int1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2942426", "long", "-long1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-235.432", "double", "-double1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-678", "float", "-float1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "235.432", "double", "-double1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "678", "float", "-float1s");

                // ~
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~bool1", "error: Operator '~' cannot be applied to operand of type 'bool'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-72", "int", "~char1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~string1", "error: Operator '~' cannot be applied to operand of type 'string'");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-36", "int", "~byte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-5278", "int", "~ushort1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "4294933039", "uint", "~uint1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "18446744073664286270", "ulong", "~ulong1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-43", "int", "~sbyte1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3156", "int", "~short1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-3550", "int", "~int1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "-2942427", "long", "~long1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "41", "int", "~sbyte1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3154", "int", "~short1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "3548", "int", "~int1s");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "2942425", "long", "~long1s");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~double1", "error: Operator '~' cannot be applied to operand of type 'double'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~double1s", "error: Operator '~' cannot be applied to operand of type 'double'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~float1", "error: Operator '~' cannot be applied to operand of type 'float'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "~float1s", "error: Operator '~' cannot be applied to operand of type 'float'");

                // !
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "false", "bool", "!bool1");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!char1", "error: Operator '!' cannot be applied to operand of type 'char'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!string1", "error: Operator '!' cannot be applied to operand of type 'string'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!byte1", "error: Operator '!' cannot be applied to operand of type 'byte'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!ushort1", "error: Operator '!' cannot be applied to operand of type 'ushort'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!uint1", "error: Operator '!' cannot be applied to operand of type 'uint'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!ulong1", "error: Operator '!' cannot be applied to operand of type 'ulong'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!sbyte1", "error: Operator '!' cannot be applied to operand of type 'sbyte'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!short1", "error: Operator '!' cannot be applied to operand of type 'short'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!int1", "error: Operator '!' cannot be applied to operand of type 'int'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!long1", "error: Operator '!' cannot be applied to operand of type 'long'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!sbyte1s", "error: Operator '!' cannot be applied to operand of type 'sbyte'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!short1s", "error: Operator '!' cannot be applied to operand of type 'short'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!int1s", "error: Operator '!' cannot be applied to operand of type 'int'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!long1s", "error: Operator '!' cannot be applied to operand of type 'long'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!double1", "error: Operator '!' cannot be applied to operand of type 'double'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!double1s", "error: Operator '!' cannot be applied to operand of type 'double'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!float1", "error: Operator '!' cannot be applied to operand of type 'float'");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "!float1s", "error: Operator '!' cannot be applied to operand of type 'float'");

                // pre ++
                // Not implemented in debugger

                // pre --
                // Not implemented in debugger

                // post ++
                // Not implemented in debugger

                // post --
                // Not implemented in debugger

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
