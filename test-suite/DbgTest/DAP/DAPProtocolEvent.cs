using System;
using System.Collections.Generic;

namespace DbgTest.DAP
{
public class Event : ProtocolMessage
{
}

public class ThreadEvent : Event
{
    public ThreadEventBody body = new();
}

public class ThreadEventBody
{
    public string reason = string.Empty;
    public int threadId;
}

public class StoppedEvent : Event
{
    public StoppedEventBody body = new();
}

public class StoppedEventBody
{
    public string reason = string.Empty;
    public string description = string.Empty;
    public int? threadId;
    public bool? preserveFocusHint;
    public string text = string.Empty;
    public bool? allThreadsStopped;
}

public class ExitedEvent : Event
{
    public ExitedEventBody body = new();
}

public class ExitedEventBody
{
    public int exitCode;
}

public class OutputEvent : Event
{
    public OutputEventBody body = new();
}

public class OutputEventBody
{
    public string category = string.Empty;
    public string output = string.Empty;
}
}
