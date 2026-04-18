using System;
using System.Collections.Generic;

namespace DbgTest.DAP
{
public class Response : ProtocolMessage
{
    public int request_seq;
    public bool success;
    public string command = string.Empty;
    public string message = string.Empty;
}

public class StackTraceResponse : Response
{
    public StackTraceResponseBody body = new();
}

public class StackTraceResponseBody
{
    public List<StackFrame> stackFrames = [];
    public int? totalFrames;
}

public class StackFrame
{
    public Int64 id;
    public string name = string.Empty;
    public Source source = new();
    public int line;
    public int column;
    public int? endLine;
    public int? endColumn;
    public string moduleId = string.Empty; // dncdbg uses string
    public string presentationHint = string.Empty; // "normal", "label", or "subtle"
}

public class ThreadsResponse : Response
{
    public ThreadsResponseBody body = new();
}

public class ThreadsResponseBody
{
    public List<Thread> threads = [];
}

public class Thread
{
    public int id;
    public string name = string.Empty;
}

public class ScopesResponse : Response
{
    public ScopesResponseBody body = new();
}

public class ScopesResponseBody
{
    public List<Scope> scopes = [];
}

public class Scope
{
    public string name = string.Empty;
    public int? variablesReference;
    public int? namedVariables;
    public int? indexedVariables;
    public bool? expensive;
    public Source source = new();
    public int? line;
    public int? column;
    public int? endLine;
    public int? endColumn;
}

public class VariablesResponse : Response
{
    public VariablesResponseBody body = new();
}

public class VariablesResponseBody
{
    public List<Variable> variables = [];
}

public class Variable
{
    public string name = string.Empty;
    public string value = string.Empty;
    public string type = string.Empty;
    public VariablePresentationHint presentationHint = new();
    public string evaluateName = string.Empty;
    public int variablesReference;
    public int? namedVariables;
    public int? indexedVariables;
}

public class VariablePresentationHint
{
    public string kind = string.Empty;
    public List<string> attributes = [];
    public string visibility = string.Empty;
}

public class EvaluateResponse : Response
{
    public EvaluateResponseBody body = new();
}

public class EvaluateResponseBody
{
    public string result = string.Empty;
    public string type = string.Empty;
    public VariablePresentationHint presentationHint = new();
    public int variablesReference;
    public int? namedVariables;
    public int? indexedVariables;
}

public class SetVariableResponse : Response
{
    public SetVariableResponseBody body = new();
}

public class SetVariableResponseBody
{
    public string value = string.Empty;
    public string type = string.Empty;
    public int? variablesReference;
    public int? namedVariables;
    public int? indexedVariables;
}

public class Breakpoint
{
    public int? id;
    public bool verified;
    public string message = string.Empty;
    public Source source = new();
    public int? line;
    public int? column;
    public int? endLine;
    public int? endColumn;
    public string instructionReference = string.Empty;
    public int? offset;
}

public class SetBreakpointsResponseBody
{
    public List<Breakpoint> breakpoints = [];
}

public class SetBreakpointsResponse : Response
{
    public SetBreakpointsResponseBody body = new();
}

public class ExceptionInfoResponse : Response
{
    public ExceptionInfoResponseBody body = new();
}

public class ExceptionInfoResponseBody
{
    public string exceptionId = string.Empty;
    public string? description;
    public string breakMode = string.Empty; // "never", "always", "unhandled", or "userUnhandled"
    public ExceptionDetails? details;
}

public class ExceptionDetails
{
    public string? message;
    public string? typeName;
    public string? fullTypeName;
    public string? evaluateName;
    public string stackTrace = string.Empty;
    public List<ExceptionDetails> innerException = [];
}

public class SetExpressionResponse : Response
{
    public SetExpressionResponseBody body = new();
}

public class SetExpressionResponseBody
{
    public string value = string.Empty;
    public string? type;
    public VariablePresentationHint? presentationHint;
    public int? variablesReference;
    public int? namedVariables;
    public int? indexedVariables;
}

public class PauseResponse : Response
{
    public PauseResponseBody body = new();
}

public class PauseResponseBody
{
    public int threadId;
}

public class Module
{
  public string id = string.Empty;
  public string name = string.Empty;
  public string? path;
  public bool? isOptimized;
  public bool? isUserCode;
  public string? version;
  public string? symbolStatus;
  public string? symbolFilePath;
  public string? dateTimeStamp;
  public string? addressRange;
}

public class ModulesResponse : Response
{
    public ModulesResponseBody body = new();
}

public class ModulesResponseBody
{
    public List<Module> modules = new();
    public int? totalModules = null;
}
}
