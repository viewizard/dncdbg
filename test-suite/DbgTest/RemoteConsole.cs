using System;
using System.IO;
using System.Net.Sockets;

namespace DbgTest
{
public class RemoteConsole : IDisposable
{
    public RemoteConsole(int port)
    {
        client = new TcpClient(AddressFamily.InterNetwork);
        client.Connect(System.Net.IPAddress.Loopback, port);
        stream = client.GetStream();
        consoleInput = new StreamWriter(stream);
        consoleOutput = new StreamReader(stream);
    }

    public void Send(string text)
    {
        consoleInput.WriteLine(text);
        consoleInput.Flush();
    }

    public void SendChar(char c)
    {
        consoleInput.Write(c);
        consoleInput.Flush();
    }

    public string? Receive(int timeout)
    {
        stream.ReadTimeout = timeout;
        return consoleOutput.ReadLine();
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        consoleInput?.Close();
        consoleOutput?.Close();
        stream?.Close();
        client?.Close();
    }

    readonly TcpClient client;
    readonly NetworkStream stream;
    readonly StreamWriter consoleInput;
    readonly StreamReader consoleOutput;
}
}
