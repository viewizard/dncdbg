using System;
using System.IO;
using System.Collections.Generic;
using DbgTestCore;
using DbgTestCore.DAP;

using Newtonsoft.Json;

namespace DbgTest.DAP
{
public class DAPResult : Tuple<bool, string>
{
    public DAPResult(bool Success, string ResponseStr) : base(Success, ResponseStr)
    {
    }

    public bool Success
    {
        get {
            return this.Item1;
        }
    }
    public string ResponseStr
    {
        get {
            return this.Item2;
        }
    }
}

public class DAPDebugger
{
    // Exception info responses may contain deeply nested self-referencing innerException chains
    // (up to the debugger's DNCDBG_INNER_EXCEPTION_WALK_LIMIT). Each nested innerException adds
    // two JSON depth levels, which exceeds Newtonsoft's default MaxDepth of 64, so allow deeper
    // nesting when parsing responses.
    const int m_maxJsonDepth = 512;

    static JsonTextReader CreateJsonReader(string stringJSON)
    {
        return new JsonTextReader(new StringReader(stringJSON)) { MaxDepth = m_maxJsonDepth };
    }

    public bool IsResponseContainProperty(string stringJSON, string testField, string testValue)
    {
        JsonTextReader reader = CreateJsonReader(stringJSON);
        while (reader.Read())
        {
            if (reader.Value != null && reader.TokenType.ToString() == "PropertyName" &&
                reader.Value.ToString() == testField && reader.Read() && reader.Value != null &&
                reader.Value.ToString() == testValue)
            {
                return true;
            }
        }

        return false;
    }

    public object? GetResponsePropertyValue(string stringJSON, string testField)
    {
        JsonTextReader reader = CreateJsonReader(stringJSON);
        while (reader.Read())
        {
            if (reader.Value != null && reader.TokenType.ToString() == "PropertyName" &&
                reader.Value.ToString() == testField && reader.Read())
            {
                return reader.Value;
            }
        }

        return null;
    }

    public DAPResult Request(Request command, bool asyncExecution = false, int timeout = -1)
    {
        command.seq = m_requestSeq++;
        string stringJSON = JsonConvert.SerializeObject(
            command, Formatting.None, new JsonSerializerSettings { NullValueHandling = NullValueHandling.Ignore });

        Logger.LogLine("-> (C) " + stringJSON);

        if (!m_client.Send(stringJSON))
        {
            throw new DebuggerNotResponses();
        }

        if (asyncExecution)
        {
            return new DAPResult(true, "success");
        }

        while (true)
        {
            string[] response = m_client.Receive(timeout) ?? throw new DebuggerNotResponses();
            string line = response[0];

            if (IsResponseContainProperty(line, "type", "response"))
            {
                Logger.LogLine("<- (R) " + line);
                if ((Int64)GetResponsePropertyValue(line, "request_seq")! == command.seq)
                    return new DAPResult((bool)GetResponsePropertyValue(line, "success")!, line);
                else
                    throw new WrongResponseSequence();
            }
            else
            {
                Logger.LogLine("<- (E) " + line);
                m_eventQueue.Enqueue(line);

                bool stopEvent = false;
                foreach (var entry in m_stopEvents)
                {
                    if (IsResponseContainProperty(line, "event", entry))
                    {
                        stopEvent = true;
                    }
                }

                if (!stopEvent)
                {
                    m_notStopEventList.Add(line);
                }
            }
        }
    }

    void ReceiveEvents(int timeout = -1)
    {
        while (true)
        {
            string[]? response = m_client.Receive(timeout);
            if (response == null)
            {
                if (timeout == DebuggerClient.PollTimeout)
                {
                    // No new messages during the poll.
                    return;
                }
                throw new DebuggerNotResponses();
            }
            string line = response[0];

            // Response for async requests
            if (IsResponseContainProperty(line, "type", "response"))
            {
                Logger.LogLine("<- (R) " + line);

                if (!(bool)GetResponsePropertyValue(line, "success")!)
                {
                    throw new FailResponse();
                }

                continue;
            }

            Logger.LogLine("<- (E) " + line);
            m_eventQueue.Enqueue(line);

            foreach (var entry in m_stopEvents)
            {
                if (IsResponseContainProperty(line, "event", entry))
                {
                    return;
                }
            }

            m_notStopEventList.Add(line);
        }
    }

    public bool IsEventReceived(Func<string, bool> filter)
    {
        // Check previously received events first.
        while (m_eventQueue.Count > 0)
        {
            if (filter(m_eventQueue.Dequeue()))
                return true;
        }

        // Receive new events and check them.
        // Note, stop events check must wait: the corresponding stop event may
        // come much later than the last response. For example, after restart
        // the newly started debuggee exits only after it finishes its work.
        ReceiveEvents(10000);
        while (m_eventQueue.Count > 0)
        {
            if (filter(m_eventQueue.Dequeue()))
                return true;
        }

        return false;
    }

    public bool IsNotStopEventReceived(Func<string, bool> filter, bool getNewEvents = false)
    {
        // Receive new events and check them.
        if (getNewEvents)
        {
            ReceiveEvents(DebuggerClient.PollTimeout);
        }
        // Check previously received events.
        foreach (var entry in m_notStopEventList)
        {
            if (filter(entry))
            {
                m_notStopEventList.Remove(entry);
                return true;
            }
        }
        return false;
    }

    public DAPDebugger(DebuggerClient debuggerClient)
    {
        m_client = debuggerClient;
    }

    Queue<string> m_eventQueue = new Queue<string>();
    List<string> m_notStopEventList = new List<string>();
    readonly DebuggerClient m_client;
    static readonly string[] m_stopEvents = { "stopped", "terminated" };
    int m_requestSeq = 1;
}
}
