using System;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestNoDebugStdIO
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testoutput",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", NoDebug: true);
            });

        Console.WriteLine("test stdout");
        System.Threading.Thread.Sleep(100);

        Console.WriteLine("test more stdout");
        System.Threading.Thread.Sleep(100);

        Console.Error.WriteLine("test stderr");
        System.Threading.Thread.Sleep(100);

        Console.Error.WriteLine("test more stderr");
        System.Threading.Thread.Sleep(100);

        Console.Error.WriteLine("test \001\002 forbidden \036\037 chars");
        System.Threading.Thread.Sleep(100);

        Label.Checkpoint("testoutput", "testinput",
            (Object context) =>
            {
                string endLine = "\n";
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    endLine = "\r\n";

                Context Context = (Context)context;
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stderr", "test stdout" + endLine, GetNewEvents: true);
                Context.FailedOutputEventCheck(@"__FILE__:__LINE__", "stdout", "test stderr" + endLine, GetNewEvents: true);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "test stdout" + endLine, GetNewEvents: true);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "test more stdout" + endLine, GetNewEvents: true);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test stderr" + endLine, GetNewEvents: true);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test more stderr" + endLine, GetNewEvents: true);
                Context.WasOutputEvent(@"__FILE__:__LINE__", "stderr", "test \u000001\u000002 forbidden \u000036\u000037 chars" + endLine, GetNewEvents: true);
            });

        Console.WriteLine("test stdin");

        string? s = Console.ReadLine();
        Console.WriteLine("input text: " + s);
        System.Threading.Thread.Sleep(100);

        Label.Checkpoint("testinput", "finish",
            (Object context) =>
            {
                System.Threading.Thread.Sleep(500);
                Context Context = (Context)context;
                Context.CalcExpressionWithStatusOnlyCheck(@"__FILE__:__LINE__", 0, "new added text", true);
                System.Threading.Thread.Sleep(500);

                string endLine = "\n";
                bool isWindows = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows);
                if (isWindows)
                    endLine = "\r\n";

                Context.WasOutputEvent(@"__FILE__:__LINE__", "stdout", "input text: new added text" + endLine, GetNewEvents: true);
                System.Threading.Thread.Sleep(500);
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
