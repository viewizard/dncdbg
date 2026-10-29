using System;
using System.IO;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

using Newtonsoft.Json;

namespace DbgTest.Script
{
class Context
{
    public void Initialize(string callerTrace, bool linesStartAt1 = true, bool columnsStartAt1 = true, bool supportsMemoryReferences = false)
    {
        m_processId = -1;
        m_threadId = -1;
        m_breakpointSourceName = string.Empty;
        m_breakpointList.Clear();
        m_breakpointLines.Clear();
        m_functionBreakpointList.Clear();

        m_exceptionFilterAll = false;
        m_exceptionFilterUserUnhandled = false;
        m_exceptionFilterAllOptions = null;
        m_exceptionFilterUserUnhandledOptions = null;
        m_exceptionFilterUnhandledOptions = null;

        m_currentBpId = 0;
        m_srcBreakpoints.Clear();
        m_srcBreakpointIds.Clear();

        m_remoteConsole = null;
        m_sourceFileMap.Clear();
        m_env.Clear();
        m_argsList.Clear();
        m_expressionEvaluationOptions = null;

        InitializeRequest initializeRequest = new InitializeRequest();
        initializeRequest.arguments.clientID = "vscode";
        initializeRequest.arguments.clientName = "Visual Studio Code";
        initializeRequest.arguments.adapterID = "coreclr";
        initializeRequest.arguments.pathFormat = "path";
        initializeRequest.arguments.linesStartAt1 = linesStartAt1;
        initializeRequest.arguments.columnsStartAt1 = columnsStartAt1;
        initializeRequest.arguments.supportsMemoryReferences = supportsMemoryReferences;
        initializeRequest.arguments.supportsVariableType = true;
        initializeRequest.arguments.supportsVariablePaging = true;
        initializeRequest.arguments.supportsRunInTerminalRequest = true;
        initializeRequest.arguments.locale = "en-us";
        Assert.True(m_dapDebugger.Request(initializeRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Launch(string callerTrace, bool? jmc = null, bool? stepFiltering = null, bool remoteConsole = false,
                       int remoteConsolePort = 0, bool asyncExecution = false, bool noDebug = false)
    {
        LaunchRequest launchRequest = new LaunchRequest();
        launchRequest.arguments.name = ".NET Core Launch (console) with pipeline";
        launchRequest.arguments.type = "coreclr";
        launchRequest.arguments.preLaunchTask = "build";
        launchRequest.arguments.program = m_controlInfo.TargetAssemblyPath!;
        launchRequest.arguments.cwd = "";
        launchRequest.arguments.stopAtEntry = true;
        launchRequest.arguments.noDebug = noDebug;

        if (m_argsList.Count != 0)
        {
            launchRequest.arguments.args = m_argsList;
        }

        if (remoteConsole)
        {
            launchRequest.arguments.env = new Dictionary<string, string>();
            launchRequest.arguments.env.Add("DNCDBG_CONSOLE", "remoteConsole");
            launchRequest.arguments.env.Add("DNCDBG_REMOTECONSOLE_PORT", remoteConsolePort.ToString());
        }
        else
        {
            launchRequest.arguments.console = "internalConsole";
        }

        if (jmc.HasValue)
        {
            launchRequest.arguments.justMyCode = jmc.Value;
        }
        if (stepFiltering.HasValue)
        {
            launchRequest.arguments.enableStepFiltering = stepFiltering.Value;
        }

        if (m_sourceFileMap.Count != 0)
        {
            launchRequest.arguments.sourceFileMap = m_sourceFileMap;
        }

        if (m_env.Count != 0)
        {
            launchRequest.arguments.env = m_env;
        }

        if (m_expressionEvaluationOptions != null)
        {
            launchRequest.arguments.expressionEvaluationOptions = m_expressionEvaluationOptions;
            m_expressionEvaluationOptions = null;
        }

        launchRequest.arguments.internalConsoleOptions = "openOnSessionStart";
        launchRequest.arguments.__sessionId = Guid.NewGuid().ToString();

        if (asyncExecution)
        {
            m_dapDebugger.Request(launchRequest, asyncExecution);
            return;
        }
        Assert.True(m_dapDebugger.Request(launchRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void StartTargetAndAttach(string callerTrace, bool startSuspend = false, bool stopAtEntry = false, bool asyncExecution = false)
    {
        Process testProcess = new Process();
        testProcess.StartInfo.UseShellExecute = false;
        testProcess.StartInfo.FileName = m_controlInfo.CorerunPath;
        testProcess.StartInfo.Arguments = m_controlInfo.TargetAssemblyPath;
        testProcess.StartInfo.CreateNoWindow = true;
        if (startSuspend)
        {
            testProcess.StartInfo.Environment["DOTNET_DefaultDiagnosticPortSuspend"] = "1";
        }
        Assert.True(testProcess.Start(), @"__FILE__:__LINE__" + "\n" + callerTrace);

        AttachRequest attachRequest = new AttachRequest();
        m_processId = testProcess.Id;
        attachRequest.arguments.processId = testProcess.Id;

        attachRequest.arguments.stopAtEntry = stopAtEntry;

        if (m_sourceFileMap.Count != 0)
        {
            attachRequest.arguments.sourceFileMap = m_sourceFileMap;
        }

        if (m_expressionEvaluationOptions != null)
        {
            attachRequest.arguments.expressionEvaluationOptions = m_expressionEvaluationOptions;
            m_expressionEvaluationOptions = null;
        }

        if (asyncExecution)
        {
            m_dapDebugger.Request(attachRequest, asyncExecution);
            return;
        }
        Assert.True(m_dapDebugger.Request(attachRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void ConfigurationDone(string callerTrace)
    {
        ConfigurationDoneRequest configurationDoneRequest = new ConfigurationDoneRequest();
        Assert.True(m_dapDebugger.Request(configurationDoneRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Restart(string callerTrace)
    {
        RestartRequest restartRequest = new RestartRequest();
        Assert.True(m_dapDebugger.Request(restartRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void RestartWithLaunchArguments(string callerTrace, bool? jmc = null, bool? stepFiltering = null)
    {
        RestartWithLaunchArgumentsRequest restartRequest = new RestartWithLaunchArgumentsRequest();
        restartRequest.arguments.arguments.name = ".NET Core Launch (console) with pipeline";
        restartRequest.arguments.arguments.type = "coreclr";
        restartRequest.arguments.arguments.program = m_controlInfo.TargetAssemblyPath!;
        restartRequest.arguments.arguments.cwd = "";
        restartRequest.arguments.arguments.stopAtEntry = true;

        if (m_argsList.Count != 0)
        {
            restartRequest.arguments.arguments.args = m_argsList;
        }

        if (jmc.HasValue)
        {
            restartRequest.arguments.arguments.justMyCode = jmc.Value;
        }
        if (stepFiltering.HasValue)
        {
            restartRequest.arguments.arguments.enableStepFiltering = stepFiltering.Value;
        }

        if (m_sourceFileMap.Count != 0)
        {
            restartRequest.arguments.arguments.sourceFileMap = m_sourceFileMap;
        }

        if (m_expressionEvaluationOptions != null)
        {
            restartRequest.arguments.arguments.expressionEvaluationOptions = m_expressionEvaluationOptions;
            m_expressionEvaluationOptions = null;
        }

        Assert.True(m_dapDebugger.Request(restartRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void RestartWithAttachArguments(string callerTrace, bool? jmc = null, bool? stepFiltering = null)
    {
        RestartWithAttachArgumentsRequest restartRequest = new RestartWithAttachArgumentsRequest();
        if (m_processId == -1)
        {
            throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
        }
        restartRequest.arguments.arguments.processId = m_processId;
        restartRequest.arguments.arguments.stopAtEntry = true;

        if (jmc.HasValue)
        {
            restartRequest.arguments.arguments.justMyCode = jmc.Value;
        }
        if (stepFiltering.HasValue)
        {
            restartRequest.arguments.arguments.enableStepFiltering = stepFiltering.Value;
        }

        if (m_sourceFileMap.Count != 0)
        {
            restartRequest.arguments.arguments.sourceFileMap = m_sourceFileMap;
        }

        if (m_expressionEvaluationOptions != null)
        {
            restartRequest.arguments.arguments.expressionEvaluationOptions = m_expressionEvaluationOptions;
            m_expressionEvaluationOptions = null;
        }

        Assert.True(m_dapDebugger.Request(restartRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasEntryPointHit(string callerTrace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "entry"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasEntryPointHitWithProperThreadID(string callerTrace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "entry"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        m_threadId = -1;
        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        Assert.True(isThreadInThreadsList(@"__FILE__:__LINE__" + "\n" + callerTrace, m_threadId),
                    @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasExit(string callerTrace, int? checkExitCode = 0)
    {
        bool wasExited = false;
        int exitCode = 0;
        bool wasTerminated = false;

        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "exited"))
            {
                wasExited = true;
                ExitedEvent exitedEvent = JsonConvert.DeserializeObject<ExitedEvent>(resJSON)!;
                exitCode = exitedEvent.body.exitCode;
            }
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "terminated"))
            {
                wasTerminated = true;
            }

            // For disconnect and unhandled exceptions, we don't check the exit code here, since Windows and Linux provide different exit codes.
            if (wasExited && checkExitCode.HasValue)
            {
                Assert.Equal(checkExitCode.Value, exitCode, @"__FILE__:__LINE__" + "\n" + callerTrace);
            }

            if (wasExited && wasTerminated)
            {
                return true;
            }

            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void AbortExecution(string callerTrace)
    {
        TerminateRequest terminateRequest = new TerminateRequest();
        terminateRequest.arguments = new TerminateArguments();
        terminateRequest.arguments.restart = false;
        Assert.True(m_dapDebugger.Request(terminateRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Detach(string callerTrace)
    {
        DetachRequest detachRequest = new DetachRequest();
        detachRequest.arguments = new DetachArguments();
        Assert.True(m_dapDebugger.Request(detachRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void DebuggerExit(string callerTrace)
    {
        DisconnectRequest disconnectRequest = new DisconnectRequest();
        disconnectRequest.arguments = new DisconnectArguments();
        disconnectRequest.arguments.restart = false;
        Assert.True(m_dapDebugger.Request(disconnectRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void AddBreakpoint(string callerTrace, string bpName, string? condition = null, string? hitCondition = null, string? logMessage = null)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;

        m_breakpointSourceName = lbp.FileName;
        m_breakpointList.Add(new SourceBreakpoint(lbp.NumLine, condition!, hitCondition!, logMessage!));
        m_breakpointLines.Add(lbp.NumLine);
    }

    public void AddBreakpointWithColumn(string callerTrace, string bpName, int column, bool linesStartAt1 = true, bool columnsStartAt1 = true)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;

        m_breakpointSourceName = lbp.FileName;
        m_breakpointList.Add(new SourceBreakpoint(linesStartAt1 ? lbp.NumLine : lbp.NumLine - 1,
                                                  columnsStartAt1 ? column : column - 1));
        m_breakpointLines.Add(lbp.NumLine);
    }

    public void RemoveBreakpoint(string callerTrace, string bpName)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;

        var bpToRemove = m_breakpointList.Find(x => x.line == lbp.NumLine);
        if (bpToRemove != null)
        {
            m_breakpointList.Remove(bpToRemove);
        }
        m_breakpointLines.Remove(lbp.NumLine);
    }

    public void AddManualBreakpointAndAddID(string callerTrace, string fileName, int line)
    {
        List<SourceBreakpoint> listBp;
        if (!m_srcBreakpoints.TryGetValue(fileName, out listBp!))
        {
            listBp = new List<SourceBreakpoint>();
            m_srcBreakpoints[fileName] = listBp;
        }
        listBp.Add(new SourceBreakpoint(line));

        List<int?> listBpId;
        if (!m_srcBreakpointIds.TryGetValue(fileName, out listBpId!))
        {
            listBpId = new List<int?>();
            m_srcBreakpointIds[fileName] = listBpId;
        }
        listBpId.Add(null);
    }

    public void WasManualBreakpointHit(string callerTrace, string fileName, int line)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(line, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(fileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void AddBreakpointAndAddID(string callerTrace, string bpName, string? bpPath = null, string? condition = null)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        List<SourceBreakpoint> listBp;
        if (!m_srcBreakpoints.TryGetValue(sourceFile, out listBp!))
        {
            listBp = new List<SourceBreakpoint>();
            m_srcBreakpoints[sourceFile] = listBp;
        }
        listBp.Add(new SourceBreakpoint(lbp.NumLine, condition!));

        List<int?> listBpId;
        if (!m_srcBreakpointIds.TryGetValue(sourceFile, out listBpId!))
        {
            listBpId = new List<int?>();
            m_srcBreakpointIds[sourceFile] = listBpId;
        }
        listBpId.Add(null);
    }

    public void RemoveBreakpointAndRemoveID(string callerTrace, string bpName, string? bpPath = null)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        Assert.True(m_srcBreakpoints.TryGetValue(sourceFile, out var listBp), @"__FILE__:__LINE__" + "\n" + callerTrace);

        Assert.True(m_srcBreakpointIds.TryGetValue(sourceFile, out var listBpId), @"__FILE__:__LINE__" + "\n" + callerTrace);

        int indexBp = listBp!.FindIndex(x => x.line == lbp.NumLine);
        listBp!.RemoveAt(indexBp);
        listBpId!.RemoveAt(indexBp);
    }

    public int? GetBreakpointId(string callerTrace, string bpName, string? bpPath = null)
    {
        Breakpoint bp = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        Assert.True(m_srcBreakpoints.TryGetValue(sourceFile, out var listBp), @"__FILE__:__LINE__" + "\n" + callerTrace);

        Assert.True(m_srcBreakpointIds.TryGetValue(sourceFile, out var listBpId), @"__FILE__:__LINE__" + "\n" + callerTrace);

        int indexBp = listBp!.FindIndex(x => x.line == lbp.NumLine);
        return listBpId![indexBp];
    }

    public void SetBreakpointsAndCheckIDs(string callerTrace)
    {
        foreach (var Breakpoints in m_srcBreakpoints)
        {
            SetBreakpointsRequest setBreakpointsRequest = new SetBreakpointsRequest();
            setBreakpointsRequest.arguments.source.name = Path.GetFileName(Breakpoints.Key);

            setBreakpointsRequest.arguments.source.path = Breakpoints.Key;
            setBreakpointsRequest.arguments.breakpoints.AddRange(Breakpoints.Value);
            setBreakpointsRequest.arguments.sourceModified = false;
            var ret = m_dapDebugger.Request(setBreakpointsRequest);
            Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

            SetBreakpointsResponse setBreakpointsResponse =
                JsonConvert.DeserializeObject<SetBreakpointsResponse>(ret.ResponseStr)!;

            // Check that breakpoints were not unexpectedly re-created with different IDs.
            for (int i = 0; i < setBreakpointsResponse.body.breakpoints.Count; i++)
            {
                if (m_srcBreakpointIds[Breakpoints.Key][i] == null)
                {
                    m_currentBpId++;
                    m_srcBreakpointIds[Breakpoints.Key][i] = setBreakpointsResponse.body.breakpoints[i].id;
                }
                else
                {
                    Assert.Equal(m_srcBreakpointIds[Breakpoints.Key][i], setBreakpointsResponse.body.breakpoints[i].id,
                                 @"__FILE__:__LINE__" + "\n" + callerTrace);
                }
            }
        }
    }

    public void AddFunctionBreakpoint(string funcName, string? condition = null, string? hitCondition = null)
    {
        m_functionBreakpointList.Add(new FunctionBreakpoint(funcName, condition!, hitCondition!));
    }

    public void RemoveFunctionBreakpoint(string funcName)
    {
        var bpToRemove = m_functionBreakpointList.Find(x => x.name == funcName);
        if (bpToRemove != null)
        {
            m_functionBreakpointList.Remove(bpToRemove);
        }
    }

    public void SetBreakpoints(string callerTrace)
    {
        SetBreakpointsRequest setBreakpointsRequest = new SetBreakpointsRequest();
        setBreakpointsRequest.arguments.source.name = m_breakpointSourceName;
        // Note: this code only works with a single source file.
        setBreakpointsRequest.arguments.source.path = m_controlInfo.SourceFilesPath!;
        setBreakpointsRequest.arguments.lines.AddRange(m_breakpointLines);
        setBreakpointsRequest.arguments.breakpoints.AddRange(m_breakpointList);
        setBreakpointsRequest.arguments.sourceModified = false;
        Assert.True(m_dapDebugger.Request(setBreakpointsRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void SetFunctionBreakpoints(string callerTrace)
    {
        SetFunctionBreakpointsRequest setFunctionBreakpointsRequest = new SetFunctionBreakpointsRequest();
        setFunctionBreakpointsRequest.arguments.breakpoints.AddRange(m_functionBreakpointList);
        Assert.True(m_dapDebugger.Request(setFunctionBreakpointsRequest).Success,
                    @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasStep(string callerTrace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "step"))
            {

                // In case of an async method, the thread could change; account for this.
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasBreakpointHit(string callerTrace, string bpName, bool checkSourcePath = true, int expectedColumn = 0, bool linesStartAt1 = true, bool columnsStartAt1 = true)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(linesStartAt1 ? lbp.NumLine : lbp.NumLine - 1,
                     stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        if (expectedColumn != 0)
        {
            Assert.Equal(columnsStartAt1 ? expectedColumn : expectedColumn - 1,
                         stackTraceResponse.body.stackFrames[0].column, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        // Note: this code works only with one source file.
        if (checkSourcePath)
        {
            Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path,
                         @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
    }

    public void WasBreakpointHitWithProperThreadID(string callerTrace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        m_threadId = -1;
        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        Assert.True(isThreadInThreadsList(@"__FILE__:__LINE__" + "\n" + callerTrace, m_threadId),
                    @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    bool isThreadInThreadsList(string callerTrace, int threadId)
    {
        if (m_threadId == -1)
        {
            return false;
        }

        ThreadsRequest threadsRequest = new ThreadsRequest();
        var ret = m_dapDebugger.Request(threadsRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ThreadsResponse threadsResponse = JsonConvert.DeserializeObject<ThreadsResponse>(ret.ResponseStr)!;

        foreach (var thread_info in threadsResponse.body.threads)
        {
            if (thread_info.id == threadId)
            {
                return true;
            }
        }

        return false;
    }

    public void WasBreakHit(string callerTrace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "pause"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Continue(string callerTrace, bool? singleThread = null, bool? expectedAllThreadsContinued = null)
    {
        ContinueRequest continueRequest = new ContinueRequest();
        continueRequest.arguments.threadId = m_threadId;
        if (singleThread.HasValue)
        {
            continueRequest.arguments.singleThread = singleThread.Value;
        }
        var ret = m_dapDebugger.Request(continueRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        if (!expectedAllThreadsContinued.HasValue)
        {
            return;
        }

        ContinueResponse continueResponse = JsonConvert.DeserializeObject<ContinueResponse>(ret.ResponseStr)!;

        Assert.Equal(expectedAllThreadsContinued.Value, continueResponse.body.allThreadsContinued!, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Pause(string callerTrace)
    {
        PauseRequest pauseRequest = new PauseRequest();
        pauseRequest.arguments.threadId = m_threadId;
        Assert.True(m_dapDebugger.Request(pauseRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasPaused(string callerTrace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "pause"))
            {
                PauseResponse pauseResponse = JsonConvert.DeserializeObject<PauseResponse>(resJSON)!;
                if (pauseResponse.body.threadId == m_threadId)
                    return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void StepOver(string callerTrace, bool? singleThread = null)
    {
        NextRequest nextRequest = new NextRequest();
        nextRequest.arguments.threadId = m_threadId;
        if (singleThread.HasValue)
        {
            nextRequest.arguments.singleThread = singleThread.Value;
        }
        Assert.True(m_dapDebugger.Request(nextRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void StepIn(string callerTrace, bool? singleThread = null)
    {
        StepInRequest stepInRequest = new StepInRequest();
        stepInRequest.arguments.threadId = m_threadId;
        if (singleThread.HasValue)
        {
            stepInRequest.arguments.singleThread = singleThread.Value;
        }
        Assert.True(m_dapDebugger.Request(stepInRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void StepOut(string callerTrace, bool? singleThread = null)
    {
        StepOutRequest stepOutRequest = new StepOutRequest();
        stepOutRequest.arguments.threadId = m_threadId;
        if (singleThread.HasValue)
        {
            stepOutRequest.arguments.singleThread = singleThread.Value;
        }
        Assert.True(m_dapDebugger.Request(stepOutRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public Int64 DetectFrameId(string callerTrace, string bpName)
    {
        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);

        return stackTraceResponse.body.stackFrames[0].id;
    }

    public void CalcAndCheckExpression(string callerTrace, Int64? frameId, string expectedResult, string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        Assert.Equal(expectedResult, evaluateResponse.body.result, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void CalcExpressionWithStatusOnlyCheck(string callerTrace, Int64? frameId, string expression, bool expectedStatus)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.Equal(expectedStatus, ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public int GetVariablesReference(string callerTrace, Int64 frameId, string scopeName)
    {
        ScopesRequest scopesRequest = new ScopesRequest();
        scopesRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(scopesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ScopesResponse scopesResponse = JsonConvert.DeserializeObject<ScopesResponse>(ret.ResponseStr)!;

        foreach (var Scope in scopesResponse.body.scopes)
        {
            if (Scope.name == scopeName)
            {
                return Scope.variablesReference == null ? 0 : (int)Scope.variablesReference;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public int GetChildVariablesReference(string callerTrace, int variablesReference, string variableName)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == variableName)
            {
                return Variable.variablesReference;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void EvalVariable(string callerTrace, int variablesReference, string type, string name, string value)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == name)
            {
                if (type != "")
                {
                    Assert.Equal(type, Variable.type, @"__FILE__:__LINE__" + "\n" + callerTrace);
                }

                var fixedVal = Variable.value;
                if (Variable.type == "char")
                {
                    int foundStr = fixedVal.IndexOf(" ");
                    if (foundStr >= 0)
                    {
                        fixedVal = fixedVal.Remove(foundStr);
                    }
                }

                Assert.Equal(value, fixedVal, @"__FILE__:__LINE__" + "\n" + callerTrace);
                return;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void CheckErrorVariable(string callerTrace, int variablesReference, string name)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == name)
            {
                throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
            }
        }
    }

    public void EvalVariableByIndex(string callerTrace, int variablesReference, string type, int index, string value)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        if (index < variablesResponse.body.variables.Count)
        {
            var Variable = variablesResponse.body.variables[index];
            Assert.Equal(type, Variable.type, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(value, Variable.value, @"__FILE__:__LINE__" + "\n" + callerTrace);
            return;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    private bool EvalVariableInt(string callerTrace, int variablesReference, string expectedResult, string expectedType,
                                 string variableName)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == variableName)
            {
                Assert.Equal(expectedType, Variable.type, @"__FILE__:__LINE__" + "\n" + callerTrace);
                Assert.Equal(expectedResult, Variable.value, @"__FILE__:__LINE__" + "\n" + callerTrace);
                return true;
            }
        }

        return false;
    }

    public void GetAndCheckChildValue(string callerTrace, Int64 frameId, string expectedResult, string expectedType,
                                      string variableName, string childName)
    {
        int refLocals = GetVariablesReference(@"__FILE__:__LINE__" + "\n" + callerTrace, frameId, "Locals");
        int refVar = GetChildVariablesReference(@"__FILE__:__LINE__" + "\n" + callerTrace, refLocals, variableName);

        if (EvalVariableInt(@"__FILE__:__LINE__" + "\n" + callerTrace, refVar, expectedResult, expectedType, childName))
        {
            return;
        }

        int refVarStatic = GetChildVariablesReference(@"__FILE__:__LINE__" + "\n" + callerTrace, refVar, "Static members");
        if (EvalVariableInt(@"__FILE__:__LINE__" + "\n" + callerTrace, refVarStatic, expectedResult, expectedType, childName))
        {
            return;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public VariablesResponse GetLocalVariables(string callerTrace, Int64 frameId)
    {
        int variablesReference = GetVariablesReference(@"__FILE__:__LINE__" + "\n" + callerTrace, frameId, "Locals");

        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        return JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;
    }

    public void CheckEnum(string callerTrace, VariablesResponse variablesResponse, string varName,
                          string expectedResult)
    {
        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == varName)
            {
                Assert.Equal(expectedResult, Variable.value, @"__FILE__:__LINE__" + "\n" + callerTrace);
                return;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void GetAndCheckValue(string callerTrace, Int64 frameId, string? expectedResult, string expectedType,
                                 string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        if (expectedResult != null)
        {
            Assert.Equal(expectedResult, evaluateResponse.body.result, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
        Assert.Equal(expectedType, evaluateResponse.body.type!, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void GetAndCheckValue(string callerTrace, Int64 frameId, string expectedResult1, string expectedResult2,
                                 string expectedType, string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        Assert.True(expectedResult1 == evaluateResponse.body.result || expectedResult2 == evaluateResponse.body.result,
                    @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(expectedType, evaluateResponse.body.type!, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public int GetAndCheckValue(string callerTrace, Int64 frameId, string expression, string expectedResult)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        var fixedVal = evaluateResponse.body.result;
        if (evaluateResponse.body.type == "char")
        {
            int foundStr = fixedVal.IndexOf(" ");
            if (foundStr >= 0)
            {
                fixedVal = fixedVal.Remove(foundStr);
            }
        }

        Assert.Equal(expectedResult, fixedVal, @"__FILE__:__LINE__" + "\n" + callerTrace);

        return evaluateResponse.body.variablesReference;
    }

    public string? GetMemoryReference(string callerTrace, Int64 frameId, string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        Assert.True(evaluateResponse.body.memoryReference != null, @"__FILE__:__LINE__" + "\n" + callerTrace);

        return evaluateResponse.body.memoryReference;
    }

    public void CheckMemoryReference(string callerTrace, Int64 frameId, string expression, bool nullReference)
    {
        string memoryReference = GetMemoryReference(@"__FILE__:__LINE__" + "\n" + callerTrace, frameId, expression)!;

        if (nullReference)
        {
            Assert.Equal("0x0000000000000000", memoryReference!, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
        else
        {
            Assert.NotEqual("0x0000000000000000", memoryReference!, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
    }

    public void CheckNoMemoryReference(string callerTrace, Int64 frameId, string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        Assert.True(evaluateResponse.body.memoryReference == null, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void CheckErrorAtRequest(string callerTrace, Int64 frameId, string expression, string errMsgStart)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.False(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        Assert.True(evaluateResponse.message!.StartsWith(errMsgStart), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void SetExpression(string callerTrace, Int64 frameId, string expression, string value)
    {
        SetExpressionRequest setExpressionRequest = new SetExpressionRequest();
        setExpressionRequest.arguments.expression = expression;
        setExpressionRequest.arguments.value = value;
        Assert.True(m_dapDebugger.Request(setExpressionRequest).Success, @"__FILE__:__LINE__");
    }

    public void ErrorSetExpression(string callerTrace, Int64 frameId, string expression, string value)
    {
        SetExpressionRequest setExpressionRequest = new SetExpressionRequest();
        setExpressionRequest.arguments.expression = expression;
        setExpressionRequest.arguments.value = value;
        Assert.False(m_dapDebugger.Request(setExpressionRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public string GetVariable(string callerTrace, Int64 frameId, string expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;

        var fixedVal = evaluateResponse.body.result;
        if (evaluateResponse.body.type == "char")
        {
            int foundStr = fixedVal.IndexOf(" ");
            if (foundStr >= 0)
            {
                fixedVal = fixedVal.Remove(foundStr);
            }
        }
        return fixedVal;
    }

    public void SetVariable(string callerTrace, Int64 frameId, int variablesReference, string name, string value,
                            bool ignoreCheck = false)
    {
        SetVariableRequest setVariableRequest = new SetVariableRequest();
        setVariableRequest.arguments.variablesReference = variablesReference;
        setVariableRequest.arguments.name = name;
        setVariableRequest.arguments.value = value;
        Assert.True(m_dapDebugger.Request(setVariableRequest).Success, @"__FILE__:__LINE__");

        if (ignoreCheck)
        {
            return;
        }

        string realValue = GetVariable(@"__FILE__:__LINE__" + "\n" + callerTrace, frameId, value);
        EvalVariable(@"__FILE__:__LINE__" + "\n" + callerTrace, variablesReference, "", name, realValue);
    }

    public void ErrorSetVariable(string callerTrace, int variablesReference, string name, string value)
    {
        SetVariableRequest setVariableRequest = new SetVariableRequest();
        setVariableRequest.arguments.variablesReference = variablesReference;
        setVariableRequest.arguments.name = name;
        setVariableRequest.arguments.value = value;
        Assert.False(m_dapDebugger.Request(setVariableRequest).Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void AddExceptionBreakpointFilterAll()
    {
        m_exceptionFilterAll = true;
    }

    public void AddExceptionBreakpointFilterAllWithConditions(string conditions)
    {
        m_exceptionFilterAllOptions = new ExceptionFilterOptions();
        m_exceptionFilterAllOptions.filterId = "all";
        m_exceptionFilterAllOptions.condition = conditions;
    }

    public void AddExceptionBreakpointFilterUserUnhandled()
    {
        m_exceptionFilterUserUnhandled = true;
    }

    public void AddExceptionBreakpointFilterUserUnhandledWithConditions(string conditions)
    {
        m_exceptionFilterUserUnhandledOptions = new ExceptionFilterOptions();
        m_exceptionFilterUserUnhandledOptions.filterId = "user-unhandled";
        m_exceptionFilterUserUnhandledOptions.condition = conditions;
    }

    public void AddExceptionBreakpointFilterUnhandledWithConditions(string conditions)
    {
        m_exceptionFilterUnhandledOptions = new ExceptionFilterOptions();
        m_exceptionFilterUnhandledOptions.filterId = "unhandled";
        m_exceptionFilterUnhandledOptions.condition = conditions;
    }

    public void ResetExceptionBreakpoints()
    {
        m_exceptionFilterAll = false;
        m_exceptionFilterUserUnhandled = false;
        m_exceptionFilterAllOptions = null;
        m_exceptionFilterUserUnhandledOptions = null;
        m_exceptionFilterUnhandledOptions = null;
    }

    public void SetExceptionBreakpoints(string callerTrace)
    {
        SetExceptionBreakpointsRequest setExceptionBreakpointsRequest = new SetExceptionBreakpointsRequest();
        if (m_exceptionFilterAll)
        {
            setExceptionBreakpointsRequest.arguments.filters.Add("all");
        }
        if (m_exceptionFilterUserUnhandled)
        {
            setExceptionBreakpointsRequest.arguments.filters.Add("user-unhandled");
        }
        // Break on unhandled exceptions added in debugger by default.

        if (m_exceptionFilterAllOptions != null ||
            m_exceptionFilterUserUnhandledOptions != null ||
            m_exceptionFilterUnhandledOptions != null)
        {
            setExceptionBreakpointsRequest.arguments.filterOptions = new List<ExceptionFilterOptions>();
        }
        if (m_exceptionFilterAllOptions != null)
        {
            setExceptionBreakpointsRequest.arguments.filterOptions.Add(m_exceptionFilterAllOptions);
        }
        if (m_exceptionFilterUserUnhandledOptions != null)
        {
            setExceptionBreakpointsRequest.arguments.filterOptions.Add(m_exceptionFilterUserUnhandledOptions);
        }
        if (m_exceptionFilterUnhandledOptions != null)
        {
            setExceptionBreakpointsRequest.arguments.filterOptions.Add(m_exceptionFilterUnhandledOptions);
        }

        Assert.True(m_dapDebugger.Request(setExceptionBreakpointsRequest).Success,
                    @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    // Exception info responses may contain deeply nested self-referencing innerException chains
    // (up to the debugger's DNCDBG_INNER_EXCEPTION_WALK_LIMIT), exceeding Newtonsoft's default
    // MaxDepth of 64; allow deeper nesting when parsing them.
    static readonly JsonSerializerSettings ExceptionInfoJsonSettings = new JsonSerializerSettings { MaxDepth = 512 };

    public void TestExceptionInfo(string callerTrace, string excCategory, string excMode, string excName)
    {
        ExceptionInfoRequest exceptionInfoRequest = new ExceptionInfoRequest();
        exceptionInfoRequest.arguments.threadId = m_threadId;
        var ret = m_dapDebugger.Request(exceptionInfoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ExceptionInfoResponse exceptionInfoResponse =
            JsonConvert.DeserializeObject<ExceptionInfoResponse>(ret.ResponseStr, ExceptionInfoJsonSettings)!;

        Assert.Equal(excMode, exceptionInfoResponse.body.breakMode, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(excCategory + "/" + excName, exceptionInfoResponse.body.exceptionId, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(excName, exceptionInfoResponse.body.details!.fullTypeName, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void TestInnerException(string callerTrace, int innerLevel, string excName, string excMessage)
    {
        ExceptionInfoRequest exceptionInfoRequest = new ExceptionInfoRequest();
        exceptionInfoRequest.arguments.threadId = m_threadId;
        var ret = m_dapDebugger.Request(exceptionInfoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ExceptionInfoResponse exceptionInfoResponse =
            JsonConvert.DeserializeObject<ExceptionInfoResponse>(ret.ResponseStr, ExceptionInfoJsonSettings)!;

        ExceptionDetails exceptionDetails = exceptionInfoResponse.body.details!.innerException![0]!;
        for (int i = 0; i < innerLevel; ++i)
        {
            exceptionDetails = exceptionDetails.innerException![0]!;
        }

        Assert.Equal(excName, exceptionDetails.fullTypeName, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(excMessage, exceptionDetails.message, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void TestInnerExceptionMessages(string callerTrace, string expectedFirst, string expectedLast)
    {
        ExceptionInfoRequest exceptionInfoRequest = new ExceptionInfoRequest();
        exceptionInfoRequest.arguments.threadId = m_threadId;
        var ret = m_dapDebugger.Request(exceptionInfoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ExceptionInfoResponse exceptionInfoResponse =
            JsonConvert.DeserializeObject<ExceptionInfoResponse>(ret.ResponseStr, ExceptionInfoJsonSettings)!;

        Assert.True(exceptionInfoResponse.body.details!.innerException.Count > 0, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(expectedFirst, exceptionInfoResponse.body.details!.innerException[0].message, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ExceptionDetails lastInner = exceptionInfoResponse.body.details!.innerException[0];
        while (true)
        {
            if (lastInner.innerException.Count == 0)
            {
                break;
            }

            lastInner = lastInner.innerException[0];
        }

        Assert.Equal(expectedLast, lastInner.message, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void TestStackTrace(string callerTrace, string[] frameNames, string[] frameLocations)
    {
        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        for (int i = 0; i < frameLocations.Length; i++)
        {
            Breakpoint bp = m_controlInfo.Breakpoints[frameLocations[i]];
            Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
            var lbp = (LineBreakpoint)bp;

            Assert.Equal(frameNames[i], stackTraceResponse.body.stackFrames[i].name, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[i].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[i].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[i].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
    }

    public void TestStackTraceLastFrame(string callerTrace, string lastFrameName, string frame)
    {
        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        int lastIndex = stackTraceResponse.body.stackFrames.Count - 1;

        Assert.Equal(stackTraceResponse.body.stackFrames[lastIndex].name, lastFrameName, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint bp = m_controlInfo.Breakpoints[frame];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)bp;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[lastIndex].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[lastIndex].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[lastIndex].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void WasExceptionBreakpointHitInExternalCode(string callerTrace, string excCategory, string excMode,
                                                        string excName, string extFrame)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "exception"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(extFrame, stackTraceResponse.body.stackFrames[0].name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        TestExceptionInfo(@"__FILE__:__LINE__" + "\n" + callerTrace, excCategory, excMode, excName);
    }

    public void WasExceptionBreakpointHit(string callerTrace, string bpName, string excCategory, string excMode, string excName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "exception"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Breakpoint breakpoint = m_controlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + callerTrace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(lbp.NumLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(lbp.FileName, stackTraceResponse.body.stackFrames[0].source!.name, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(m_controlInfo.SourceFilesPath, stackTraceResponse.body.stackFrames[0].source!.path, @"__FILE__:__LINE__" + "\n" + callerTrace);

        TestExceptionInfo(@"__FILE__:__LINE__" + "\n" + callerTrace, excCategory, excMode, excName);
    }

    public void GetResultAsString(string callerTrace, Int64 frameId, string expr, out string strRes)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expr;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;
        strRes = evaluateResponse.body.result;
    }

    public int GetExpressionEvaluationReference(string callerTrace, Int64 frameId, string expr)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expr;
        evaluateRequest.arguments.frameId = frameId;
        var ret = m_dapDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr)!;
        return evaluateResponse.body.variablesReference;
    }

    public void WasOutputEvent(string callerTrace, string category, string output, bool getNewEvents = false)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "output"))
            {
                OutputEvent outputEvent = JsonConvert.DeserializeObject<OutputEvent>(resJSON)!;
                if (outputEvent.body.category == category && outputEvent.body.output == output)
                {
                    return true;
                }
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsNotStopEventReceived(filter, getNewEvents), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void FailedOutputEventCheck(string callerTrace, string category, string output, bool getNewEvents = false)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "output"))
            {
                OutputEvent outputEvent = JsonConvert.DeserializeObject<OutputEvent>(resJSON)!;
                if (outputEvent.body.category == category && outputEvent.body.output == output)
                {
                    return false;
                }
            }
            return true;
        };

        Assert.True(m_dapDebugger.IsNotStopEventReceived(filter, getNewEvents), @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void TestModules(string callerTrace, int startModule, int moduleCount, bool checkMainDLLExistenceInList)
    {
        ModulesRequest modulesRequest = new ModulesRequest();
        modulesRequest.arguments.startModule = startModule;
        modulesRequest.arguments.moduleCount = moduleCount;
        var ret = m_dapDebugger.Request(modulesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ModulesResponse modulesResponse = JsonConvert.DeserializeObject<ModulesResponse>(ret.ResponseStr)!;
        string AbsolutePathToAssembly = Path.GetFullPath(m_controlInfo.TargetAssemblyPath!);

        foreach (Module module in modulesResponse.body.modules)
        {
            if (AbsolutePathToAssembly == module.path)
            {
                if (checkMainDLLExistenceInList)
                {
                    return;
                }
                else
                {
                    throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
                }
            }
        }

        if (checkMainDLLExistenceInList)
        {
            throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + callerTrace);
        }
    }

    public void CheckVariableNameCount(string callerTrace, int variablesReference, string name, int expectedCount)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = m_dapDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        VariablesResponse variablesResponse =
            JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr)!;

        int count = 0;
        foreach (var variable in variablesResponse.body.variables)
        {
            if (variable.name == name)
            {
                count++;
            }
        }

        Assert.Equal(expectedCount, count, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void GetGotoTargets(string callerTrace, string fileName, int line, int column, int expectedCount)
    {
        GotoTargetsRequest gotoTargetsRequest = new GotoTargetsRequest();
        gotoTargetsRequest.arguments.source.name = fileName;
        gotoTargetsRequest.arguments.source.path = fileName;
        gotoTargetsRequest.arguments.line = line;
        gotoTargetsRequest.arguments.column = column;

        var ret = m_dapDebugger.Request(gotoTargetsRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        GotoTargetsResponse gotoTargetsResponse =
            JsonConvert.DeserializeObject<GotoTargetsResponse>(ret.ResponseStr)!;

        Assert.Equal(expectedCount, gotoTargetsResponse.body.targets.Count, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void CheckErrorGotoTargets(string callerTrace, string fileName, int line, int column)
    {
        GotoTargetsRequest gotoTargetsRequest = new GotoTargetsRequest();
        gotoTargetsRequest.arguments.source.name = fileName;
        gotoTargetsRequest.arguments.source.path = fileName;
        gotoTargetsRequest.arguments.line = line;
        gotoTargetsRequest.arguments.column = column;

        var ret = m_dapDebugger.Request(gotoTargetsRequest);
        Assert.False(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void Goto(string callerTrace, int targetID, int expectedLine, int expectedColumn)
    {
        GotoRequest gotoRequest = new GotoRequest();
        gotoRequest.arguments.threadId = m_threadId;
        gotoRequest.arguments.targetId = targetID;

        var ret = m_dapDebugger.Request(gotoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        Func<string, bool> filter = (resJSON) =>
        {
            if (m_dapDebugger.IsResponseContainProperty(resJSON, "event", "stopped") &&
                m_dapDebugger.IsResponseContainProperty(resJSON, "reason", "goto"))
            {
                m_threadId = Convert.ToInt32(m_dapDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(m_dapDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 50;
        ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;

        Assert.Equal(expectedLine, stackTraceResponse.body.stackFrames[0].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
        Assert.Equal(expectedColumn, stackTraceResponse.body.stackFrames[0].column, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void CheckErrorGoto(string callerTrace, int targetID)
    {
        GotoRequest gotoRequest = new GotoRequest();
        gotoRequest.arguments.threadId = m_threadId;
        gotoRequest.arguments.targetId = targetID;

        var ret = m_dapDebugger.Request(gotoRequest);
        Assert.False(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public int GetSourceReferenceFromTopFrame(string callerTrace)
    {
        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = m_threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 1;
        var ret = m_dapDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr)!;
        return stackTraceResponse.body.stackFrames[0].source.sourceReference ?? 0;
    }

    public int GetSourceReferenceFromLoadedSources(string callerTrace, string sourceName)
    {
        LoadedSourcesRequest loadedSourcesRequest = new LoadedSourcesRequest();
        var ret = m_dapDebugger.Request(loadedSourcesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        LoadedSourcesResponse loadedSourcesResponse = JsonConvert.DeserializeObject<LoadedSourcesResponse>(ret.ResponseStr)!;

        int sourceReference = 0;
        for (int i = 0; i < loadedSourcesResponse.body.sources.Count; i++)
        {
            if (loadedSourcesResponse.body.sources[i].name == sourceName)
            {
                sourceReference = loadedSourcesResponse.body.sources[i].sourceReference ?? 0;
                break;
            }
        }

        Assert.True(sourceReference != 0, @"__FILE__:__LINE__" + "\n" + callerTrace);
        return sourceReference;
    }

    public void CompareSources(string callerTrace, string fileName, int sourceReference, string sourcePath)
    {
        SourceRequest sourceRequest = new SourceRequest();
        sourceRequest.arguments.sourceReference = sourceReference;
        sourceRequest.arguments.source.sourceReference = sourceReference;
        sourceRequest.arguments.source.path = sourcePath;
        var ret = m_dapDebugger.Request(sourceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        SourceResponse sourceResponse = JsonConvert.DeserializeObject<SourceResponse>(ret.ResponseStr)!;

        string[] parts = m_controlInfo.SourceFilesPath!.Split(';', StringSplitOptions.RemoveEmptyEntries);
        string filePath = Path.Combine(Path.GetDirectoryName(parts[0])!, fileName);
        string fileContent = File.ReadAllText(filePath);

        Assert.True(fileContent == sourceResponse.body.content, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public void TestBreakpointLocations(string callerTrace, BreakpointLocation[] expectedLocations, string fileName, int line, int column, int endLine, int endColumn)
    {
        BreakpointLocationsRequest breakpointLocationsRequest = new BreakpointLocationsRequest();
        breakpointLocationsRequest.arguments.source.path = fileName;
        breakpointLocationsRequest.arguments.line = line;
        breakpointLocationsRequest.arguments.column = column;
        breakpointLocationsRequest.arguments.endLine = endLine;
        breakpointLocationsRequest.arguments.endColumn = endColumn;
        var ret = m_dapDebugger.Request(breakpointLocationsRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        BreakpointLocationsResponse breakpointLocationsResponse = JsonConvert.DeserializeObject<BreakpointLocationsResponse>(ret.ResponseStr)!;

        Assert.True(expectedLocations.Length == breakpointLocationsResponse.body.breakpoints.Count, @"__FILE__:__LINE__" + "\n" + callerTrace);

        for (int i = 0; i < breakpointLocationsResponse.body.breakpoints.Count; i++)
        {
            Assert.Equal(expectedLocations[i].line, breakpointLocationsResponse.body.breakpoints[i].line, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(expectedLocations[i].column, breakpointLocationsResponse.body.breakpoints[i].column, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(expectedLocations[i].endLine, breakpointLocationsResponse.body.breakpoints[i].endLine, @"__FILE__:__LINE__" + "\n" + callerTrace);
            Assert.Equal(expectedLocations[i].endColumn, breakpointLocationsResponse.body.breakpoints[i].endColumn, @"__FILE__:__LINE__" + "\n" + callerTrace);
        }
    }

    public string ReadMemory(string callerTrace, string cemoryReference, int count)
    {
        ReadMemoryRequest readMemoryRequest = new ReadMemoryRequest();
        readMemoryRequest.arguments.memoryReference = cemoryReference;
        readMemoryRequest.arguments.count = count;
        var ret = m_dapDebugger.Request(readMemoryRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);

        ReadMemoryResponse readMemoryResponse = JsonConvert.DeserializeObject<ReadMemoryResponse>(ret.ResponseStr)!;

        Assert.Equal(cemoryReference, readMemoryResponse.body.address, @"__FILE__:__LINE__" + "\n" + callerTrace);

        return readMemoryResponse.body.data!;
    }

    public void WriteMemory(string callerTrace, string cemoryReference, string dataBase64)
    {
        WriteMemoryRequest writeMemoryRequest = new WriteMemoryRequest();
        writeMemoryRequest.arguments.memoryReference = cemoryReference;
        writeMemoryRequest.arguments.data = dataBase64;
        var ret = m_dapDebugger.Request(writeMemoryRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + callerTrace);
    }

    public string? GetSourceFilesPath()
    {
        return m_controlInfo.SourceFilesPath;
    }

    public void AddSourceFileMapEntry(string oldPath, string newPath)
    {
        m_sourceFileMap.Add(oldPath, newPath);
    }

    public void AddArgsListEntry(string newArg)
    {
        m_argsList.Add(newArg);
    }

    public void AddEnvMapEntry(string envName, string envValue)
    {
        m_env.Add(envName, envValue);
    }

    public Context(ControlInfo controlInfo, DbgTestCore.DebuggerClient debuggerClient)
    {
        m_controlInfo = controlInfo;
        m_dapDebugger = new DAPDebugger(debuggerClient);
    }

    ControlInfo m_controlInfo;
    DAPDebugger m_dapDebugger;
    int m_processId = -1;
    int m_threadId = -1;
    string m_breakpointSourceName = string.Empty;
    List<SourceBreakpoint> m_breakpointList = new List<SourceBreakpoint>();
    List<int> m_breakpointLines = new List<int>();
    List<FunctionBreakpoint> m_functionBreakpointList = new List<FunctionBreakpoint>();

    bool m_exceptionFilterAll = false;
    bool m_exceptionFilterUserUnhandled = false;
    ExceptionFilterOptions? m_exceptionFilterAllOptions = null;
    ExceptionFilterOptions? m_exceptionFilterUserUnhandledOptions = null;
    ExceptionFilterOptions? m_exceptionFilterUnhandledOptions = null;

    public int m_currentBpId = 0;
    // Note: m_srcBreakpoints and m_srcBreakpointIds must have the same order of elements, since we use indexes for mapping.
    Dictionary<string, List<SourceBreakpoint>> m_srcBreakpoints = new Dictionary<string, List<SourceBreakpoint>>();
    Dictionary<string, List<int?>> m_srcBreakpointIds = new Dictionary<string, List<int?>>();

    public RemoteConsole? m_remoteConsole = null;
    Dictionary<string, string> m_sourceFileMap = new Dictionary<string, string>();
    Dictionary<string, string> m_env = new Dictionary<string, string>();
    List<string> m_argsList = new List<string>();
    public ExpressionEvaluationOptions? m_expressionEvaluationOptions = null;
}
}
