using System;
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
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");

                int port = 33212;
                Context.Launch(@"__FILE__:__LINE__", remoteConsole: true, remoteConsolePort: port);
                System.Threading.Thread.Sleep(1000);
                Context.m_remoteConsole = new RemoteConsole(port);

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
        Console.Write("" + (char)c1 + (char)c2 + (char)c3 + (char)c4 + (char)c5);
        Console.Out.Flush();

        Label.Checkpoint("testremote", "finish",
            (Object context) =>
            {
                System.Threading.Thread.Sleep(1000);

                Context Context = (Context)context;

                string? text = Context.m_remoteConsole!.Receive(1000);
                Assert.Equal("test remote console, input text:", text, @"__FILE__:__LINE__");

                Context.m_remoteConsole.Send("new added text");

                text = Context.m_remoteConsole.Receive(1000);
                Assert.Equal("text: new added text", text, @"__FILE__:__LINE__");

                Context.m_remoteConsole.Send("more text 12345");

                text = Context.m_remoteConsole.Receive(1000);
                Assert.Equal("new text: more text 12345", text, @"__FILE__:__LINE__");

                Context.m_remoteConsole.SendChar('c');
                Context.m_remoteConsole.SendChar('h');
                Context.m_remoteConsole.SendChar('a');
                Context.m_remoteConsole.SendChar('r');
                Context.m_remoteConsole.SendChar('s');

                int? ch = Context.m_remoteConsole.ReceiveChar(1000);
                Assert.Equal((int)'c', ch, @"__FILE__:__LINE__");
                ch = Context.m_remoteConsole.ReceiveChar(1000);
                Assert.Equal((int)'h', ch, @"__FILE__:__LINE__");
                ch = Context.m_remoteConsole.ReceiveChar(1000);
                Assert.Equal((int)'a', ch, @"__FILE__:__LINE__");
                ch = Context.m_remoteConsole.ReceiveChar(1000);
                Assert.Equal((int)'r', ch, @"__FILE__:__LINE__");
                ch = Context.m_remoteConsole.ReceiveChar(1000);
                Assert.Equal((int)'s', ch, @"__FILE__:__LINE__");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
                Context.m_remoteConsole!.Close();
            });
    }
}
}
