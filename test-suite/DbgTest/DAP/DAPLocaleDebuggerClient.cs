using System;
using System.Collections.Concurrent;
using System.IO;
using System.Threading;
using System.Text;

using DbgTestCore;
using DbgTest;

namespace DbgTestCore.DAP
{
public class DAPLocalDebuggerClient : DebuggerClient
{
    public DAPLocalDebuggerClient(StreamWriter input, StreamReader output)
    {
        DebuggerInput = input;
        DebuggerOutput = output;
        ReceivedMessages = new ConcurrentQueue<string>();
        DataAvailable = new SemaphoreSlim(0);
        InputThread = new Thread(ReaderThread) { IsBackground = true };
        InputThread.Start();
    }

    public override bool DoHandshake(int timeout)
    {
        return true;
    }

    public override bool Send(string command)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(command);
        string commandSize = bytes.Length.ToString();
        DebuggerInput.Write(CONTENT_LENGTH + commandSize + TWO_CRLF + command);
        DebuggerInput.Flush();

        return true;
    }

    public override string[]? Receive(int timeout)
    {
        string? line = ReceiveOutputLine(timeout);
        if (line is null)
        {
            return null;
        }
        return [line];
    }

    public override void Close()
    {
        DebuggerInput.Close();
        DebuggerOutput.Close();
    }

    string? ReadData()
    {
        try
        {
            string header = "";
            byte[] recvBuffer = new byte[1];

            while (true)
            {
                // Read until "\r\n\r\n"
                int readCount = DebuggerOutput.BaseStream.Read(recvBuffer, 0, recvBuffer.Length);
                if (readCount == 0)
                {
                    // End of the stream: the debugger adapter is gone.
                    return null;
                }
                header += Encoding.ASCII.GetString(recvBuffer, 0, readCount);

                if (header.Length < TWO_CRLF.Length)
                {
                    continue;
                }

                if (header.Substring(header.Length - TWO_CRLF.Length, TWO_CRLF.Length) != TWO_CRLF)
                {
                    continue;
                }

                // Extract Content-Length
                int lengthIndex = header.IndexOf(CONTENT_LENGTH);
                if (lengthIndex == -1)
                {
                    continue;
                }

                int contentLength = Int32.Parse(header.Substring(lengthIndex + CONTENT_LENGTH.Length));

                byte[] buffer = new byte[contentLength + 1];
                buffer[contentLength] = 0;
                int buffer_i = 0;
                while (buffer_i < contentLength)
                {
                    int count = DebuggerOutput.BaseStream.Read(buffer, buffer_i, contentLength - buffer_i);
                    if (count == 0)
                    {
                        // End of the stream in the middle of the message.
                        return null;
                    }
                    buffer_i += count;
                }

                return Encoding.UTF8.GetString(buffer);
            }
        }
        catch (SystemException ex)
            when (ex is InvalidOperationException || ex is IOException || ex is ObjectDisposedException)
        {
            // The stream is closed or the read was interrupted (for example,
            // EINTR during the session teardown). Report the end of the
            // stream instead of crashing the reader thread.
            return null;
        }
        // unreachable
    }

    void ReaderThread()
    {
        // Read messages continuously, independently of the Receive() calls: the DAP
        // debugger may pipeline several messages (for example, the `exited` and
        // `terminated` events are emitted back-to-back when the NoDebug process exits),
        // while a read triggered per Receive() call can be abandoned by a timed-out
        // poll, which loses or duplicates messages (see ReceiveOutputLine() below).
        while (true)
        {
            string? message;
            try
            {
                message = ReadData();
            }
            catch (Exception ex)
            {
                // The reader thread must never take the whole test process
                // down: report the failure as the end of the stream.
                message = null;
                try
                {
                    Logger.LogLine("DAPLocalDebuggerClient: read failed: " + ex.Message);
                }
                catch
                {
                    // Logging must not crash the reader thread.
                }
            }

            if (message is null)
            {
                // End of the stream: the debugger adapter is gone.
                EofReached = true;
                DataAvailable.Release();
                return;
            }

            ReceivedMessages.Enqueue(message);
            DataAvailable.Release();
        }
    }

    string? ReceiveOutputLine(int timeout)
    {
        while (true)
        {
            if (ReceivedMessages.TryDequeue(out string? queued))
            {
                return queued;
            }

            if (EofReached)
            {
                return null;
            }

            // Wait for the reader thread to deliver a message. For the poll receive
            // (DebuggerClient.PollTimeout) wait for a short interval and return null
            // when there is nothing, instead of waiting for the full timeout or
            // throwing DebuggerNotResponses.
            if (!DataAvailable.Wait(timeout == PollTimeout ? PollIntervalMs : timeout))
            {
                if (timeout == PollTimeout)
                {
                    // Nothing new during the poll.
                    return null;
                }

                if (EofReached)
                {
                    // The adapter is gone.
                    return null;
                }

                throw new DebuggerNotResponses();
            }
        }
    }

    // Interval in milliseconds for the poll receive (DebuggerClient.PollTimeout).
    const int PollIntervalMs = 100;

    readonly StreamWriter DebuggerInput;
    readonly StreamReader DebuggerOutput;
    readonly Thread InputThread;
    readonly ConcurrentQueue<string> ReceivedMessages;
    readonly SemaphoreSlim DataAvailable;
    volatile bool EofReached;
    static readonly string TWO_CRLF = "\r\n\r\n";
    static readonly string CONTENT_LENGTH = "Content-Length: ";
}
}
