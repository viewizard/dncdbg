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
    public void PrepareStart(bool? JMC, bool? StepFiltering, string caller_trace)
    {
        InitializeRequest initializeRequest = new InitializeRequest();
        initializeRequest.arguments.clientID = "vscode";
        initializeRequest.arguments.clientName = "Visual Studio Code";
        initializeRequest.arguments.adapterID = "coreclr";
        initializeRequest.arguments.pathFormat = "path";
        initializeRequest.arguments.linesStartAt1 = true;
        initializeRequest.arguments.columnsStartAt1 = true;
        initializeRequest.arguments.supportsVariableType = true;
        initializeRequest.arguments.supportsVariablePaging = true;
        initializeRequest.arguments.supportsRunInTerminalRequest = true;
        initializeRequest.arguments.locale = "en-us";
        Assert.True(DAPDebugger.Request(initializeRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        LaunchRequest launchRequest = new LaunchRequest();
        launchRequest.arguments.name = ".NET Core Launch (console) with pipeline";
        launchRequest.arguments.type = "coreclr";
        launchRequest.arguments.preLaunchTask = "build";
        launchRequest.arguments.program = ControlInfo.TargetAssemblyPath;
        launchRequest.arguments.cwd = "";
        launchRequest.arguments.console = "internalConsole";
        launchRequest.arguments.stopAtEntry = true;
        if (JMC.HasValue)
        {
            launchRequest.arguments.justMyCode = JMC.Value;
        }
        if (StepFiltering.HasValue)
        {
            launchRequest.arguments.enableStepFiltering = StepFiltering.Value;
        }
        launchRequest.arguments.internalConsoleOptions = "openOnSessionStart";
        launchRequest.arguments.__sessionId = Guid.NewGuid().ToString();
        Assert.True(DAPDebugger.Request(launchRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void PrepareStartAttach(string caller_trace)
    {
        InitializeRequest initializeRequest = new InitializeRequest();
        initializeRequest.arguments.clientID = "vscode";
        initializeRequest.arguments.clientName = "Visual Studio Code";
        initializeRequest.arguments.adapterID = "coreclr";
        initializeRequest.arguments.pathFormat = "path";
        initializeRequest.arguments.linesStartAt1 = true;
        initializeRequest.arguments.columnsStartAt1 = true;
        initializeRequest.arguments.supportsVariableType = true;
        initializeRequest.arguments.supportsVariablePaging = true;
        initializeRequest.arguments.supportsRunInTerminalRequest = true;
        initializeRequest.arguments.locale = "en-us";
        Assert.True(DAPDebugger.Request(initializeRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Process testProcess = new Process();
        testProcess.StartInfo.UseShellExecute = false;
        testProcess.StartInfo.FileName = ControlInfo.CorerunPath;
        testProcess.StartInfo.Arguments = ControlInfo.TargetAssemblyPath;
        testProcess.StartInfo.CreateNoWindow = true;
        Assert.True(testProcess.Start(), @"__FILE__:__LINE__" + "\n" + caller_trace);

        AttachRequest attachRequest = new AttachRequest();
        attachRequest.arguments.processId = testProcess.Id;
        Assert.True(DAPDebugger.Request(attachRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void PrepareStartWithEnv(string caller_trace)
    {
        InitializeRequest initializeRequest = new InitializeRequest();
        initializeRequest.arguments.clientID = "vscode";
        initializeRequest.arguments.clientName = "Visual Studio Code";
        initializeRequest.arguments.adapterID = "coreclr";
        initializeRequest.arguments.pathFormat = "path";
        initializeRequest.arguments.linesStartAt1 = true;
        initializeRequest.arguments.columnsStartAt1 = true;
        initializeRequest.arguments.supportsVariableType = true;
        initializeRequest.arguments.supportsVariablePaging = true;
        initializeRequest.arguments.supportsRunInTerminalRequest = true;
        initializeRequest.arguments.locale = "en-us";
        Assert.True(DAPDebugger.Request(initializeRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        LaunchRequest launchRequest = new LaunchRequest();
        launchRequest.arguments.name = ".NET Core Launch (web)";
        launchRequest.arguments.type = "coreclr";
        launchRequest.arguments.preLaunchTask = "build";

        string AbsolutePathToAssembly = Path.GetFullPath(ControlInfo.TargetAssemblyPath);
        launchRequest.arguments.program = Path.GetFileName(AbsolutePathToAssembly);
        string targetAssemblyPath = Path.GetFileName(AbsolutePathToAssembly);
        int subLength = AbsolutePathToAssembly.Length - targetAssemblyPath.Length;
        string dllPath = AbsolutePathToAssembly.Substring(0, subLength);
        launchRequest.arguments.cwd = dllPath;

        launchRequest.arguments.env = new Dictionary<string, string>();
        launchRequest.arguments.env.Add("ASPNETCORE_ENVIRONMENT", "Development");
        launchRequest.arguments.env.Add("ASPNETCORE_URLS", "https://localhost:25001");
        launchRequest.arguments.console = "internalConsole";
        launchRequest.arguments.stopAtEntry = true;
        launchRequest.arguments.internalConsoleOptions = "openOnSessionStart";
        launchRequest.arguments.__sessionId = Guid.NewGuid().ToString();
        Assert.True(DAPDebugger.Request(launchRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void PrepareEnd(string caller_trace)
    {
        ConfigurationDoneRequest configurationDoneRequest = new ConfigurationDoneRequest();
        Assert.True(DAPDebugger.Request(configurationDoneRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasEntryPointHit(string caller_trace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "entry"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasEntryPointHitWithProperThreadID(string caller_trace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "entry"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        threadId = -1;
        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        Assert.True(isThredInThreadsList(@"__FILE__:__LINE__" + "\n" + caller_trace, threadId),
                    @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasExit(int? checkExitCode, string caller_trace)
    {
        bool wasExited = false;
        int exitCode = 0;
        bool wasTerminated = false;

        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "exited"))
            {
                wasExited = true;
                ExitedEvent exitedEvent = JsonConvert.DeserializeObject<ExitedEvent>(resJSON);
                exitCode = exitedEvent.body.exitCode;
            }
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "terminated"))
            {
                wasTerminated = true;
            }

            // for disconnect and unhandled exception we don't check exit code here, since Windows and Linux provide different exit code
            if (wasExited && checkExitCode.HasValue)
                Assert.Equal(exitCode, checkExitCode.Value, @"__FILE__:__LINE__" + "\n" + caller_trace);

            if (wasExited && wasTerminated)
                return true;

            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void AbortExecution(string caller_trace)
    {
        TerminateRequest terminateRequest = new TerminateRequest();
        terminateRequest.arguments = new TerminateArguments();
        terminateRequest.arguments.restart = false;
        Assert.True(DAPDebugger.Request(terminateRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void DebuggerExit(string caller_trace)
    {
        DisconnectRequest disconnectRequest = new DisconnectRequest();
        disconnectRequest.arguments = new DisconnectArguments();
        disconnectRequest.arguments.restart = false;
        Assert.True(DAPDebugger.Request(disconnectRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void AddBreakpoint(string caller_trace, string bpName, string Condition = null)
    {
        Breakpoint bp = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)bp;

        BreakpointSourceName = lbp.FileName;
        BreakpointList.Add(new SourceBreakpoint(lbp.NumLine, Condition));
        BreakpointLines.Add(lbp.NumLine);
    }

    public void RemoveBreakpoint(string caller_trace, string bpName)
    {
        Breakpoint bp = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)bp;

        BreakpointList.Remove(BreakpointList.Find(x => x.line == lbp.NumLine));
        BreakpointLines.Remove(lbp.NumLine);
    }

    public void AddManualBreakpointAndAddID(string caller_trace, string bp_fileName, int bp_line)
    {
        List<SourceBreakpoint> listBp;
        if (!SrcBreakpoints.TryGetValue(bp_fileName, out listBp))
        {
            listBp = new List<SourceBreakpoint>();
            SrcBreakpoints[bp_fileName] = listBp;
        }
        listBp.Add(new SourceBreakpoint(bp_line, null));

        List<int?> listBpId;
        if (!SrcBreakpointIds.TryGetValue(bp_fileName, out listBpId))
        {
            listBpId = new List<int?>();
            SrcBreakpointIds[bp_fileName] = listBpId;
        }
        listBpId.Add(null);
    }

    public void WasManualBreakpointHit(string caller_trace, string bp_fileName, int bp_line)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == bp_line &&
            stackTraceResponse.body.stackFrames[0].source.name == bp_fileName)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void AddBreakpointAndAddID(string caller_trace, string bpName, string bpPath = null, string Condition = null)
    {
        Breakpoint bp = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        List<SourceBreakpoint> listBp;
        if (!SrcBreakpoints.TryGetValue(sourceFile, out listBp))
        {
            listBp = new List<SourceBreakpoint>();
            SrcBreakpoints[sourceFile] = listBp;
        }
        listBp.Add(new SourceBreakpoint(lbp.NumLine, Condition));

        List<int?> listBpId;
        if (!SrcBreakpointIds.TryGetValue(sourceFile, out listBpId))
        {
            listBpId = new List<int?>();
            SrcBreakpointIds[sourceFile] = listBpId;
        }
        listBpId.Add(null);
    }

    public void RemoveBreakpointAndRemoveID(string caller_trace, string bpName, string bpPath = null)
    {
        Breakpoint bp = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        List<SourceBreakpoint> listBp;
        Assert.True(SrcBreakpoints.TryGetValue(sourceFile, out listBp), @"__FILE__:__LINE__" + "\n" + caller_trace);

        List<int?> listBpId;
        Assert.True(SrcBreakpointIds.TryGetValue(sourceFile, out listBpId), @"__FILE__:__LINE__" + "\n" + caller_trace);

        int indexBp = listBp.FindIndex(x => x.line == lbp.NumLine);
        listBp.RemoveAt(indexBp);
        listBpId.RemoveAt(indexBp);
    }

    public int? GetBreakpointId(string caller_trace, string bpName, string bpPath = null)
    {
        Breakpoint bp = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)bp;
        string sourceFile = bpPath != null ? bpPath : lbp.FileName;

        List<SourceBreakpoint> listBp;
        Assert.True(SrcBreakpoints.TryGetValue(sourceFile, out listBp), @"__FILE__:__LINE__" + "\n" + caller_trace);

        List<int?> listBpId;
        Assert.True(SrcBreakpointIds.TryGetValue(sourceFile, out listBpId), @"__FILE__:__LINE__" + "\n" + caller_trace);

        int indexBp = listBp.FindIndex(x => x.line == lbp.NumLine);
        return listBpId[indexBp];
    }

    public void SetBreakpointsAndCheckIDs(string caller_trace)
    {
        foreach (var Breakpoints in SrcBreakpoints)
        {
            SetBreakpointsRequest setBreakpointsRequest = new SetBreakpointsRequest();
            setBreakpointsRequest.arguments.source.name = Path.GetFileName(Breakpoints.Key);

            setBreakpointsRequest.arguments.source.path = Breakpoints.Key;
            setBreakpointsRequest.arguments.breakpoints.AddRange(Breakpoints.Value);
            setBreakpointsRequest.arguments.sourceModified = false;
            var ret = DAPDebugger.Request(setBreakpointsRequest);
            Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

            SetBreakpointsResponse setBreakpointsResponse =
                JsonConvert.DeserializeObject<SetBreakpointsResponse>(ret.ResponseStr);

            // check, that we don't have hiddenly re-created breakpoints with different ids
            for (int i = 0; i < setBreakpointsResponse.body.breakpoints.Count; i++)
            {
                if (SrcBreakpointIds[Breakpoints.Key][i] == null)
                {
                    CurrentBpId++;
                    SrcBreakpointIds[Breakpoints.Key][i] = setBreakpointsResponse.body.breakpoints[i].id;
                }
                else
                {
                    Assert.Equal(SrcBreakpointIds[Breakpoints.Key][i], setBreakpointsResponse.body.breakpoints[i].id,
                                 @"__FILE__:__LINE__" + "\n" + caller_trace);
                }
            }
        }
    }

    public void AddFunctionBreakpoint(string funcName, string Condition = null)
    {
        FunctionBreakpointList.Add(new FunctionBreakpoint(funcName, Condition));
    }

    public void RemoveFunctionBreakpoint(string funcName)
    {
        FunctionBreakpointList.Remove(FunctionBreakpointList.Find(x => x.name == funcName));
    }

    public void SetBreakpoints(string caller_trace)
    {
        SetBreakpointsRequest setBreakpointsRequest = new SetBreakpointsRequest();
        setBreakpointsRequest.arguments.source.name = BreakpointSourceName;
        // NOTE this code works only with one source file
        setBreakpointsRequest.arguments.source.path = ControlInfo.SourceFilesPath;
        setBreakpointsRequest.arguments.lines.AddRange(BreakpointLines);
        setBreakpointsRequest.arguments.breakpoints.AddRange(BreakpointList);
        setBreakpointsRequest.arguments.sourceModified = false;
        Assert.True(DAPDebugger.Request(setBreakpointsRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void SetFunctionBreakpoints(string caller_trace)
    {
        SetFunctionBreakpointsRequest setFunctionBreakpointsRequest = new SetFunctionBreakpointsRequest();
        setFunctionBreakpointsRequest.arguments.breakpoints.AddRange(FunctionBreakpointList);
        Assert.True(DAPDebugger.Request(setFunctionBreakpointsRequest).Success,
                    @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasStep(string caller_trace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "step"))
            {

                // In case of async method, thread could be changed, care about this.
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasBreakpointHit(string caller_trace, string bpName, bool checkSourcePath = true)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && (!checkSourcePath || (checkSourcePath && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)))
            return;

        throw new ResultNotSuccessException(@"stackTraceResponse.body.stackFrames[0].source.path" + "\n" + caller_trace);
    }

    public void WasBreakpointHitWithProperThreadID(string caller_trace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "breakpoint"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        threadId = -1;
        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        Assert.True(isThredInThreadsList(@"__FILE__:__LINE__" + "\n" + caller_trace, threadId),
                    @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    bool isThredInThreadsList(string caller_trace, int ThreadId)
    {
        if (threadId == -1)
        {
            return false;
        }

        ThreadsRequest threadsRequest = new ThreadsRequest();
        var ret = DAPDebugger.Request(threadsRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        ThreadsResponse threadsResponse = JsonConvert.DeserializeObject<ThreadsResponse>(ret.ResponseStr);

        foreach (var thread_info in threadsResponse.body.threads)
        {
            if (thread_info.id == ThreadId)
            {
                return true;
            }
        }

        return false;
    }

    public void WasBreakHit(string caller_trace, string bpName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "pause"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void Continue(string caller_trace)
    {
        ContinueRequest continueRequest = new ContinueRequest();
        continueRequest.arguments.threadId = threadId;
        Assert.True(DAPDebugger.Request(continueRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void Pause(string caller_trace)
    {
        PauseRequest pauseRequest = new PauseRequest();
        pauseRequest.arguments.threadId = threadId;
        Assert.True(DAPDebugger.Request(pauseRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasPaused(string caller_trace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "pause"))
            {
                PauseResponse pauseResponse = JsonConvert.DeserializeObject<PauseResponse>(resJSON);
                if (pauseResponse.body.threadId == threadId)
                    return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void StepOver(string caller_trace)
    {
        NextRequest nextRequest = new NextRequest();
        nextRequest.arguments.threadId = threadId;
        Assert.True(DAPDebugger.Request(nextRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void StepIn(string caller_trace)
    {
        StepInRequest stepInRequest = new StepInRequest();
        stepInRequest.arguments.threadId = threadId;
        Assert.True(DAPDebugger.Request(stepInRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void StepOut(string caller_trace)
    {
        StepOutRequest stepOutRequest = new StepOutRequest();
        stepOutRequest.arguments.threadId = threadId;
        Assert.True(DAPDebugger.Request(stepOutRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public Int64 DetectFrameId(string caller_trace, string bpName)
    {
        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)
            return stackTraceResponse.body.stackFrames[0].id;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void CalcAndCheckExpression(string caller_trace, Int64? frameId, string ExpectedResult, string Expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        Assert.Equal(ExpectedResult, evaluateResponse.body.result, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void CalcExpressionWithNotDeclared(string caller_trace, Int64? frameId, string Expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.False(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public int GetVariablesReference(string caller_trace, Int64 frameId, string ScopeName)
    {
        ScopesRequest scopesRequest = new ScopesRequest();
        scopesRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(scopesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        ScopesResponse scopesResponse = JsonConvert.DeserializeObject<ScopesResponse>(ret.ResponseStr);

        foreach (var Scope in scopesResponse.body.scopes)
        {
            if (Scope.name == ScopeName)
            {
                return Scope.variablesReference == null ? 0 : (int)Scope.variablesReference;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public int GetChildVariablesReference(string caller_trace, int VariablesReference, string VariableName)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = VariablesReference;
        var ret = DAPDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr);

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == VariableName)
                return Variable.variablesReference;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void EvalVariable(string caller_trace, int variablesReference, string Type, string Name, string Value)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = DAPDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr);

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == Name)
            {
                if (Type != "")
                    Assert.Equal(Type, Variable.type, @"__FILE__:__LINE__" + "\n" + caller_trace);

                var fixedVal = Variable.value;
                if (Variable.type == "char")
                {
                    int foundStr = fixedVal.IndexOf(" ");
                    if (foundStr >= 0)
                        fixedVal = fixedVal.Remove(foundStr);
                }

                Assert.Equal(Value, fixedVal, @"__FILE__:__LINE__" + "\n" + caller_trace);
                return;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void EvalVariableByIndex(string caller_trace, int variablesReference, string Type, int Index, string Value)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = DAPDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr);

        if (Index < variablesResponse.body.variables.Count)
        {
            var Variable = variablesResponse.body.variables[Index];
            Assert.Equal(Type, Variable.type, @"__FILE__:__LINE__" + "\n" + caller_trace);
            Assert.Equal(Value, Variable.value, @"__FILE__:__LINE__" + "\n" + caller_trace);
            return;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    private bool _EvalVariable(string caller_trace, int variablesReference, string ExpectedResult, string ExpectedType,
                             string VariableName)
    {
        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = DAPDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        VariablesResponse variablesResponse = JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr);

        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == VariableName)
            {
                Assert.Equal(ExpectedType, Variable.type, @"__FILE__:__LINE__" + "\n" + caller_trace);
                Assert.Equal(ExpectedResult, Variable.value, @"__FILE__:__LINE__" + "\n" + caller_trace);
                return true;
            }
        }

        return false;
    }

    public void GetAndCheckChildValue(string caller_trace, Int64 frameId, string ExpectedResult, string ExpectedType,
                                      string VariableName, string ChildName)
    {
        int refLocals = GetVariablesReference(@"__FILE__:__LINE__" + "\n" + caller_trace, frameId, "Locals");
        int refVar = GetChildVariablesReference(@"__FILE__:__LINE__" + "\n" + caller_trace, refLocals, VariableName);

        if (_EvalVariable(@"__FILE__:__LINE__" + "\n" + caller_trace, refVar, ExpectedResult, ExpectedType, ChildName))
            return;

        int refVarStatic =
            GetChildVariablesReference(@"__FILE__:__LINE__" + "\n" + caller_trace, refVar, "Static members");
        if (_EvalVariable(@"__FILE__:__LINE__" + "\n" + caller_trace, refVarStatic, ExpectedResult, ExpectedType, ChildName))
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public VariablesResponse GetLocalVariables(string caller_trace, Int64 frameId)
    {
        int variablesReference = GetVariablesReference(@"__FILE__:__LINE__" + "\n" + caller_trace, frameId, "Locals");

        VariablesRequest variablesRequest = new VariablesRequest();
        variablesRequest.arguments.variablesReference = variablesReference;
        var ret = DAPDebugger.Request(variablesRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        return JsonConvert.DeserializeObject<VariablesResponse>(ret.ResponseStr);
    }

    public void CheckEnum(string caller_trace, VariablesResponse variablesResponse, string VarName,
                          string ExpectedResult)
    {
        foreach (var Variable in variablesResponse.body.variables)
        {
            if (Variable.name == VarName)
            {
                Assert.Equal(ExpectedResult, Variable.value, @"__FILE__:__LINE__" + "\n" + caller_trace);
                return;
            }
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void GetAndCheckValue(string caller_trace, Int64 frameId, string ExpectedResult, string ExpectedType,
                                 string Expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        Assert.Equal(ExpectedResult, evaluateResponse.body.result, @"__FILE__:__LINE__" + "\n" + caller_trace);
        Assert.Equal(ExpectedType, evaluateResponse.body.type, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void GetAndCheckValue(string caller_trace, Int64 frameId, string ExpectedResult1, string ExpectedResult2,
                                 string ExpectedType, string Expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        Assert.True(ExpectedResult1 == evaluateResponse.body.result || ExpectedResult2 == evaluateResponse.body.result,
                    @"__FILE__:__LINE__" + "\n" + caller_trace);
        Assert.Equal(ExpectedType, evaluateResponse.body.type, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void GetAndCheckValue(string caller_trace, Int64 frameId, string Expression, string ExpectedResult)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        var fixedVal = evaluateResponse.body.result;
        if (evaluateResponse.body.type == "char")
        {
            int foundStr = fixedVal.IndexOf(" ");
            if (foundStr >= 0)
                fixedVal = fixedVal.Remove(foundStr);
        }

        Assert.Equal(ExpectedResult, fixedVal, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void CheckErrorAtRequest(string caller_trace, Int64 frameId, string Expression, string errMsgStart)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.False(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        Assert.True(evaluateResponse.message.StartsWith(errMsgStart), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void SetExpression(string caller_trace, Int64 frameId, string Expression, string Value)
    {
        SetExpressionRequest setExpressionRequest = new SetExpressionRequest();
        setExpressionRequest.arguments.expression = Expression;
        setExpressionRequest.arguments.value = Value;
        Assert.True(DAPDebugger.Request(setExpressionRequest).Success, @"__FILE__:__LINE__");
    }

    public void ErrorSetExpression(string caller_trace, Int64 frameId, string Expression, string Value)
    {
        SetExpressionRequest setExpressionRequest = new SetExpressionRequest();
        setExpressionRequest.arguments.expression = Expression;
        setExpressionRequest.arguments.value = Value;
        Assert.False(DAPDebugger.Request(setExpressionRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public string GetVariable(string caller_trace, Int64 frameId, string Expression)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = Expression;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);

        var fixedVal = evaluateResponse.body.result;
        if (evaluateResponse.body.type == "char")
        {
            int foundStr = fixedVal.IndexOf(" ");
            if (foundStr >= 0)
                fixedVal = fixedVal.Remove(foundStr);
        }
        return fixedVal;
    }

    public void SetVariable(string caller_trace, Int64 frameId, int variablesReference, string Name, string Value,
                            bool ignoreCheck = false)
    {
        SetVariableRequest setVariableRequest = new SetVariableRequest();
        setVariableRequest.arguments.variablesReference = variablesReference;
        setVariableRequest.arguments.name = Name;
        setVariableRequest.arguments.value = Value;
        Assert.True(DAPDebugger.Request(setVariableRequest).Success, @"__FILE__:__LINE__");

        if (ignoreCheck)
            return;

        string realValue = GetVariable(@"__FILE__:__LINE__" + "\n" + caller_trace, frameId, Value);
        EvalVariable(@"__FILE__:__LINE__" + "\n" + caller_trace, variablesReference, "", Name, realValue);
    }

    public void ErrorSetVariable(string caller_trace, int variablesReference, string Name, string Value)
    {
        SetVariableRequest setVariableRequest = new SetVariableRequest();
        setVariableRequest.arguments.variablesReference = variablesReference;
        setVariableRequest.arguments.name = Name;
        setVariableRequest.arguments.value = Value;
        Assert.False(DAPDebugger.Request(setVariableRequest).Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void AddExceptionBreakpointFilterAll()
    {
        ExceptionFilterAll = true;
    }

    public void AddExceptionBreakpointFilterAllWithOptions(string options)
    {
        ExceptionFilterAllOptions = new ExceptionFilterOptions();
        ExceptionFilterAllOptions.filterId = "all";
        ExceptionFilterAllOptions.condition = options;
    }

    public void AddExceptionBreakpointFilterUserUnhandled()
    {
        ExceptionFilterUserUnhandled = true;
    }

    public void AddExceptionBreakpointFilterUserUnhandledWithOptions(string options)
    {
        ExceptionFilterUserUnhandledOptions = new ExceptionFilterOptions();
        ExceptionFilterUserUnhandledOptions.filterId = "user-unhandled";
        ExceptionFilterUserUnhandledOptions.condition = options;
    }

    public void ResetExceptionBreakpoints()
    {
        ExceptionFilterAll = false;
        ExceptionFilterUserUnhandled = false;
        ExceptionFilterAllOptions = null;
        ExceptionFilterUserUnhandledOptions = null;
    }

    public void SetExceptionBreakpoints(string caller_trace)
    {
        SetExceptionBreakpointsRequest setExceptionBreakpointsRequest = new SetExceptionBreakpointsRequest();
        if (ExceptionFilterAll)
            setExceptionBreakpointsRequest.arguments.filters.Add("all");
        if (ExceptionFilterUserUnhandled)
            setExceptionBreakpointsRequest.arguments.filters.Add("user-unhandled");

        if (ExceptionFilterAllOptions != null || ExceptionFilterUserUnhandledOptions != null)
            setExceptionBreakpointsRequest.arguments.filterOptions = new List<ExceptionFilterOptions>();
        if (ExceptionFilterAllOptions != null)
            setExceptionBreakpointsRequest.arguments.filterOptions.Add(ExceptionFilterAllOptions);
        if (ExceptionFilterUserUnhandledOptions != null)
            setExceptionBreakpointsRequest.arguments.filterOptions.Add(ExceptionFilterUserUnhandledOptions);

        Assert.True(DAPDebugger.Request(setExceptionBreakpointsRequest).Success,
                    @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void TestExceptionInfo(string caller_trace, string excCategory, string excMode, string excName)
    {
        ExceptionInfoRequest exceptionInfoRequest = new ExceptionInfoRequest();
        exceptionInfoRequest.arguments.threadId = threadId;
        var ret = DAPDebugger.Request(exceptionInfoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        ExceptionInfoResponse exceptionInfoResponse =
            JsonConvert.DeserializeObject<ExceptionInfoResponse>(ret.ResponseStr);

        if (exceptionInfoResponse.body.breakMode == excMode &&
            exceptionInfoResponse.body.exceptionId == excCategory + "/" + excName &&
            exceptionInfoResponse.body.details.fullTypeName == excName)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void TestInnerException(string caller_trace, int innerLevel, string excName, string excMessage)
    {
        ExceptionInfoRequest exceptionInfoRequest = new ExceptionInfoRequest();
        exceptionInfoRequest.arguments.threadId = threadId;
        var ret = DAPDebugger.Request(exceptionInfoRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        ExceptionInfoResponse exceptionInfoResponse =
            JsonConvert.DeserializeObject<ExceptionInfoResponse>(ret.ResponseStr);

        ExceptionDetails exceptionDetails = exceptionInfoResponse.body.details.innerException[0];
        for (int i = 0; i < innerLevel; ++i)
            exceptionDetails = exceptionDetails.innerException[0];

        if (exceptionDetails.fullTypeName == excName && exceptionDetails.message == excMessage)
            return;

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void TestExceptionStackTrace(string caller_trace, string[] stacktrace, int num)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "exception"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        for (int i = 0; i < num; i++)
        {
            Breakpoint bp = ControlInfo.Breakpoints[stacktrace[i]];
            Assert.Equal(BreakpointType.Line, bp.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
            var lbp = (LineBreakpoint)bp;

            if (lbp.FileName != stackTraceResponse.body.stackFrames[i].source.name ||
                ControlInfo.SourceFilesPath != stackTraceResponse.body.stackFrames[i].source.path ||
                lbp.NumLine != stackTraceResponse.body.stackFrames[i].line)
            {
                throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
            }
        }
        return;
    }

    public void WasExceptionBreakpointHitInExternalCode(string caller_trace, string excCategory, string excMode,
                                                        string excName, string extFrame)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "exception"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].name == extFrame)
        {
            TestExceptionInfo(@"__FILE__:__LINE__" + "\n" + caller_trace, excCategory, excMode, excName);
            return;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void WasExceptionBreakpointHit(string caller_trace, string bpName, string excCategory, string excMode, string excName)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "stopped") &&
                DAPDebugger.isResponseContainProperty(resJSON, "reason", "exception"))
            {
                threadId = Convert.ToInt32(DAPDebugger.GetResponsePropertyValue(resJSON, "threadId"));
                return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);

        StackTraceRequest stackTraceRequest = new StackTraceRequest();
        stackTraceRequest.arguments.threadId = threadId;
        stackTraceRequest.arguments.startFrame = 0;
        stackTraceRequest.arguments.levels = 20;
        var ret = DAPDebugger.Request(stackTraceRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);

        Breakpoint breakpoint = ControlInfo.Breakpoints[bpName];
        Assert.Equal(BreakpointType.Line, breakpoint.Type, @"__FILE__:__LINE__" + "\n" + caller_trace);
        var lbp = (LineBreakpoint)breakpoint;

        StackTraceResponse stackTraceResponse = JsonConvert.DeserializeObject<StackTraceResponse>(ret.ResponseStr);

        if (stackTraceResponse.body.stackFrames[0].line == lbp.NumLine &&
            stackTraceResponse.body.stackFrames[0].source.name == lbp.FileName
            // NOTE this code works only with one source file
            && stackTraceResponse.body.stackFrames[0].source.path == ControlInfo.SourceFilesPath)
        {
            TestExceptionInfo(@"__FILE__:__LINE__" + "\n" + caller_trace, excCategory, excMode, excName);
            return;
        }

        throw new ResultNotSuccessException(@"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void GetResultAsString(string caller_trace, Int64 frameId, string expr, out string strRes)
    {
        EvaluateRequest evaluateRequest = new EvaluateRequest();
        evaluateRequest.arguments.expression = expr;
        evaluateRequest.arguments.frameId = frameId;
        var ret = DAPDebugger.Request(evaluateRequest);
        Assert.True(ret.Success, @"__FILE__:__LINE__" + "\n" + caller_trace);
        EvaluateResponse evaluateResponse = JsonConvert.DeserializeObject<EvaluateResponse>(ret.ResponseStr);
        strRes = evaluateResponse.body.result;
    }

    public void WasOutputEvent(string category, string output, string caller_trace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "output"))
            {
                OutputEvent outputEvent = JsonConvert.DeserializeObject<OutputEvent>(resJSON);
                if (outputEvent.body.category == category && outputEvent.body.output == output)
                    return true;
            }
            return false;
        };

        Assert.True(DAPDebugger.IsNotStopEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public void FailedOutputEventCheck(string category, string output, string caller_trace)
    {
        Func<string, bool> filter = (resJSON) =>
        {
            if (DAPDebugger.isResponseContainProperty(resJSON, "event", "output"))
            {
                OutputEvent outputEvent = JsonConvert.DeserializeObject<OutputEvent>(resJSON);
                if (outputEvent.body.category == category && outputEvent.body.output == output)
                    return false;
            }
            return true;
        };

        Assert.True(DAPDebugger.IsNotStopEventReceived(filter), @"__FILE__:__LINE__" + "\n" + caller_trace);
    }

    public Context(ControlInfo controlInfo, DbgTestCore.DebuggerClient debuggerClient)
    {
        ControlInfo = controlInfo;
        DAPDebugger = new DAPDebugger(debuggerClient);
    }

    ControlInfo ControlInfo;
    DAPDebugger DAPDebugger;
    int threadId = -1;
    string BreakpointSourceName;
    List<SourceBreakpoint> BreakpointList = new List<SourceBreakpoint>();
    List<int> BreakpointLines = new List<int>();
    List<FunctionBreakpoint> FunctionBreakpointList = new List<FunctionBreakpoint>();

    bool ExceptionFilterAll = false;
    bool ExceptionFilterUserUnhandled = false;
    ExceptionFilterOptions ExceptionFilterAllOptions = null;
    ExceptionFilterOptions ExceptionFilterUserUnhandledOptions = null;

    public int CurrentBpId = 0;
    // Note, SrcBreakpoints and SrcBreakpointIds must have same order of the elements, since we use indexes for mapping.
    Dictionary<string, List<SourceBreakpoint>> SrcBreakpoints = new Dictionary<string, List<SourceBreakpoint>>();
    Dictionary<string, List<int?>> SrcBreakpointIds = new Dictionary<string, List<int?>>();
}
}
