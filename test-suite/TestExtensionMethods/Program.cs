using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;

using DNCDbgTest;
using DNCDbgTest.DAP;
using DNCDbgTest.Script;

namespace CustomExtensions
{
public static class StringExtension
{
    public static int WordCount(this string str)
    {
        return str.Split(new char[] { ' ', '.', '?' }, StringSplitOptions.RemoveEmptyEntries).Length;
    }

    public static int WordCount(this string str, int i)
    {
        return str.Split(new char[] { ' ', '.', '?' }, StringSplitOptions.RemoveEmptyEntries).Length + i;
    }

    public static int WordCount(this string str, int i, int j)
    {
        return str.Split(new char[] { ' ', '.', '?' }, StringSplitOptions.RemoveEmptyEntries).Length + i + j;
    }
}
}

namespace TestExtensionMethods
{
using CustomExtensions;

public class MyString
{
    string s;
    public MyString(string ms)
    {
        s = ms;
    }
}

struct MyInt
{
    int i;
    public MyInt(int mi)
    {
        i = mi;
    }
}

class Program
{
    static void Main(string[] args)
    {
        string s = "The quick brown fox jumped over the lazy dog.";
        string st = "first.second";
        List<string> lists = new List<string>();

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

        lists.Add("null");
        lists.Add("first");
        lists.Add("second");
        lists.Add("third");
        lists.Add("fourth");
        string res = lists.ElementAt(1);                                   Label.Breakpoint("BREAK1");

        Label.Checkpoint(
            "bp_test", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "9", "int", "s.WordCount()");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "10", "int", "s.WordCount(1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount(1+1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount( 1+1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount(1+1 )");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount( 1+1 )");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount( 1 + 1 )");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount(1,1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "13", "int", "s.WordCount(1+1,1+1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount(1,1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "11", "int", "s.WordCount(1,1)");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "s.WordCount(1,1,1)", "error: 0x80070057");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "s.WordCount(\"first\")", "error: 0x80070057");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "s.WordCount(1, \"first\")", "error: 0x80070057");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "s.WordCount(\"first\", 1)", "error: 0x80070057");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"null\"", "string", "lists.ElementAt(0)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"first\"", "string", "lists.ElementAt(1)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"second\"", "string", "lists.ElementAt(2)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"third\"", "string", "lists.ElementAt(3)");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"fourth\"", "string", "lists.ElementAt(4)");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lists.ElemetAt()", "error: 0x80070057");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lists.ElementAt(1,2)", "error: 0x80070057");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "lists.ElementAt(\"first\")", "error: 0x80070057");

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
