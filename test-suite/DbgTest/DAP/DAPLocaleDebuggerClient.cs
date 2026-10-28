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
        m_debuggerInput = input;
        m_debuggerOutput = output;
        m_receivedMessages = new ConcurrentQueue<string>();
        m_dataAvailable = new SemaphoreSlim(0);
        m_inputThread = new Thread(ReaderThread) { IsBackground = true };
        m_inputThread.Start();
    }

    public override bool DoHandshake(int timeout)
    {
        return true;
    }

    public override bool Send(string command)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(command);
        string commandSize = bytes.Length.ToString();
        m_debuggerInput.Write(m_contentLength + commandSize + m_twoCRLF + command);
        m_debuggerInput.Flush();

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
        m_debuggerInput.Close();
        m_debuggerOutput.Close();
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
                int readCount = m_debuggerOutput.BaseStream.Read(recvBuffer, 0, recvBuffer.Length);
                if (readCount == 0)
                {
                    // End of the stream: the debugger adapter is gone.
                    return null;
                }
                header += Encoding.ASCII.GetString(recvBuffer, 0, readCount);

                if (header.Length < m_twoCRLF.Length)
                {
                    continue;
                }

                if (header.Substring(header.Length - m_twoCRLF.Length, m_twoCRLF.Length) != m_twoCRLF)
                {
                    continue;
                }

                // Extract Content-Length
                int lengthIndex = header.IndexOf(m_contentLength);
                if (lengthIndex == -1)
                {
                    continue;
                }

                int contentLength = Int32.Parse(header.Substring(lengthIndex + m_contentLength.Length));

                byte[] buffer = new byte[contentLength + 1];
                buffer[contentLength] = 0;
                int buffer_i = 0;
                while (buffer_i < contentLength)
                {
                    int count = m_debuggerOutput.BaseStream.Read(buffer, buffer_i, contentLength - buffer_i);
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
                m_eofReached = true;
                m_dataAvailable.Release();
                return;
            }

            m_receivedMessages.Enqueue(message);
            m_dataAvailable.Release();
        }
    }

    string? ReceiveOutputLine(int timeout)
    {
        while (true)
        {
            if (m_receivedMessages.TryDequeue(out string? queued))
            {
                return queued;
            }

            if (m_eofReached)
            {
                return null;
            }

            // Wait for the reader thread to deliver a message. For the poll receive
            // (DebuggerClient.PollTimeout) wait for a short interval and return null
            // when there is nothing, instead of waiting for the full timeout or
            // throwing DebuggerNotResponses.
            if (!m_dataAvailable.Wait(timeout == PollTimeout ? m_pollIntervalMs : timeout))
            {
                if (timeout == PollTimeout)
                {
                    // Nothing new during the poll.
                    return null;
                }

                if (m_eofReached)
                {
                    // The adapter is gone.
                    return null;
                }

                throw new DebuggerNotResponses();
            }
        }
    }

    // Interval in milliseconds for the poll receive (DebuggerClient.PollTimeout).
    const int m_pollIntervalMs = 100;

    readonly StreamWriter m_debuggerInput;
    readonly StreamReader m_debuggerOutput;
    readonly Thread m_inputThread;
    readonly ConcurrentQueue<string> m_receivedMessages;
    readonly SemaphoreSlim m_dataAvailable;
    volatile bool m_eofReached;
    static readonly string m_twoCRLF = "\r\n\r\n";
    static readonly string m_contentLength = "Content-Length: ";
}
}
