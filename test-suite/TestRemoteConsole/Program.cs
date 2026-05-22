using System;
using System.IO;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestRemoteConsole
{
class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "testremote",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                int port = 33212;
                Context.Launch(JMC: null, StepFiltering: null, RemoteConsole: true, RemoteConsolePort: port, @"__FILE__:__LINE__");

                System.Threading.Thread.Sleep(1000);
                Context.RemoteConsole = new RemoteConsole(port);

                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        Console.WriteLine("test remote console, input text:");
        string? s = Console.ReadLine();
        Console.WriteLine("text: " + s);
        s = Console.ReadLine();
        Console.WriteLine("new text: " + s);

        int c1 = Console.Read();
        int c2 = Console.Read();
        int c3 = Console.Read();
        int c4 = Console.Read();
        int c5 = Console.Read();
        Console.WriteLine("new chars: " + (char)c1 + (char)c2 + (char)c3 + (char)c4 + (char)c5);

        Label.Checkpoint("testremote", "finish",
            (Object context) =>
            {
                System.Threading.Thread.Sleep(1000);

                Context Context = (Context)context;

                string? text = Context.RemoteConsole!.Receive(1000);
                Assert.Equal("test remote console, input text:", text, @"__FILE__:__LINE__");

                Context.RemoteConsole.Send("new added text");

                text = Context.RemoteConsole.Receive(1000);
                Assert.Equal("text: new added text", text, @"__FILE__:__LINE__");

                Context.RemoteConsole.Send("more text 12345");

                text = Context.RemoteConsole.Receive(1000);
                Assert.Equal("new text: more text 12345", text, @"__FILE__:__LINE__");

                Context.RemoteConsole.SendChar('c');
                Context.RemoteConsole.SendChar('h');
                Context.RemoteConsole.SendChar('a');
                Context.RemoteConsole.SendChar('r');
                Context.RemoteConsole.SendChar('s');

                text = Context.RemoteConsole.Receive(1000);
                Assert.Equal("new chars: chars", text, @"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(0, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
                Context.RemoteConsole!.Close();
            });
    }
}
}
