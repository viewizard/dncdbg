# Debug Adapter Protocol Support Status

This document shows, message by message and field by field, which parts of the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) are supported by DNCDbg.

How to read the blocks below:

- Field names, signatures, and their order are identical to the official DAP specification.
- For **requests**, ✅ marks fields the debugger accepts and uses, ❌ marks fields it ignores.
- For **responses** and **events**, ✅ marks fields the debugger sends to the client.
- For **types**, ✅ marks fields the debugger fills in or reads.
- ❌ marks fields that are not supported.
- 🧩 marks fields that go beyond the DAP specification and follow the VS Code C# debugger conventions.
- Messages missing from the detailed sections below are not supported at all; every such message is listed explicitly in the overview.
- ⚪ marks a message that carries no additional fields.

## Notation

```text
✅  Supported
❌  Not supported
🧩  VS Code IDE additional field
ℹ️  Note
⚪  No additional fields
```

## Navigation

#### Base Protocol

[ProtocolMessage](#protocolmessage), [Request](#request), [Event](#event), [Response](#response), [ErrorResponse](#errorresponse), [Cancel Request](#cancelrequest-cancel)

#### Events

[Initialized Event](#initializedevent-initialized), [Stopped Event](#stoppedevent), [Continued Event](#continuedevent), [Exited Event](#exitedevent), [Terminated Event](#terminatedevent), [Thread Event](#threadevent), [Output Event](#outputevent), [Breakpoint Event](#breakpointevent), [Module Event](#moduleevent), [LoadedSource Event](#loadedsourceevent), [Process Event](#processevent), [Capabilities Event](#capabilitiesevent)

#### Requests

[Initialize Request](#initializerequest-initialize), [Launch Request](#launchrequest-launch), [Attach Request](#attachrequest-attach), [Disconnect Request](#disconnectrequest-disconnect), [Terminate Request](#terminaterequest-terminate), [BreakpointLocations Request](#breakpointlocationsrequest-breakpointlocations), [SetBreakpoints Request](#setbreakpointsrequest-setbreakpoints), [SetFunctionBreakpoints Request](#setfunctionbreakpointsrequest-setfunctionbreakpoints), [SetExceptionBreakpoints Request](#setexceptionbreakpointsrequest-setexceptionbreakpoints), [ConfigurationDone Request](#configurationdonerequest-configurationdone), [Continue Request](#continuerequest-continue), [Next Request](#nextrequest-next), [StepIn Request](#stepinrequest-stepin), [StepOut Request](#stepoutrequest-stepout), [Pause Request](#pauserequest-pause), [Goto Request](#gotorequest-goto), [StackTrace Request](#stacktracerequest-stacktrace), [Scopes Request](#scopesrequest-scopes), [Variables Request](#variablesrequest-variables), [SetVariable Request](#setvariablerequest-setvariable), [Source Request](#sourcerequest-source), [Threads Request](#threadsrequest-threads), [Modules Request](#modulesrequest-modules), [LoadedSources Request](#loadedsourcesrequest-loadedsources), [Evaluate Request](#evaluaterequest-evaluate), [SetExpression Request](#setexpressionrequest-setexpression), [GotoTargets Request](#gototargetsrequest-gototargets), [ExceptionInfo Request](#exceptioninforequest-exceptioninfo)

#### Types

[Capabilities](#capabilities), [Checksum](#checksum), [ExceptionBreakpointsFilter](#exceptionbreakpointsfilter), [Module](#module), [Thread](#thread), [Source](#source), [StackFrame](#stackframe), [Scope](#scope), [Variable](#variable), [SourceBreakpoint](#sourcebreakpoint), [FunctionBreakpoint](#functionbreakpoint), [Breakpoint](#breakpoint), [BreakpointLocation](#breakpointlocation), [GotoTarget](#gototarget), [ExceptionFilterOptions](#exceptionfilteroptions), [ExceptionOptions](#exceptionoptions), [ExceptionDetails](#exceptiondetails), [ExpressionEvaluationOptions](#expressionevaluationoptions)

## Base Protocol

#### ProtocolMessage
```text
✅  seq: number;
✅  type: 'request' | 'response' | 'event' | string;
```
#### Request
```text
✅  command: string;
✅  arguments?: any;
```
#### Event
```text
✅  event: string;
✅  body?: any;
```
#### Response
```text
✅  request_seq: number;
✅  success: boolean;
✅  command: string;
✅  message?: 'cancelled' | 'notStopped' | string;
✅  body?: any;
```
#### ErrorResponse
```text
❌  error?: Message;
```
#### CancelRequest `cancel`
```text
✅  requestId?: number;
❌  progressId?: string;
```
#### CancelResponse
```text
⚪  no additional fields
```

## Events

#### InitializedEvent `initialized`
```text
⚪  no additional fields
```
#### StoppedEvent
```text
✅  reason: 'step' | 'breakpoint' | 'exception' | 'pause'
        | 'entry' | 'function breakpoint' | 'data breakpoint'
        | 'instruction breakpoint' | 'goto' | string;
        ℹ️  note: 'data breakpoint' and 'instruction breakpoint' are never sent
❌  description?: string;
✅  threadId?: number;
❌  preserveFocusHint?: boolean;
✅  text?: string;
✅  allThreadsStopped?: boolean;
✅  hitBreakpointIds?: number[];
```
#### ContinuedEvent
```text
✅  threadId: number;
✅  allThreadsContinued?: boolean;
```
#### ExitedEvent
```text
✅  exitCode: number;
```
#### TerminatedEvent
```text
❌  restart?: any;
```
#### ThreadEvent
```text
✅  reason: 'started' | 'exited' | string;
✅  threadId: number;
```
#### OutputEvent
```text
✅  category?: 'console' | 'important' | 'telemetry' | 'stdout' | 'stderr' | string;
        ℹ️  note: 'important' and 'telemetry' are never sent
✅  output: string;
❌  group?: 'start' | 'startCollapsed' | 'end';
❌  variablesReference?: number;
✅  source?: Source;
✅  line?: number;
✅  column?: number;
❌  data?: any;
❌  locationReference?: number;
```
#### BreakpointEvent
```text
✅  reason: 'changed' | 'new' | 'removed' | string;
✅  breakpoint: Breakpoint;
```
#### ModuleEvent
```text
✅  reason: 'new' | 'changed' | 'removed';
✅  module: Module;
```
#### LoadedSourceEvent
```text
✅  reason: 'new' | 'changed' | 'removed';
✅  source: Source;
```
#### ProcessEvent
```text
✅  name: string;
✅  systemProcessId?: number;
✅  isLocalProcess?: boolean;
✅  startMethod?: 'launch' | 'attach' | 'attachForSuspendedLaunch';
        ℹ️  note: 'attachForSuspendedLaunch' is never sent
✅  pointerSize?: number;
```
#### CapabilitiesEvent
```text
✅  capabilities: Capabilities;
```

## Requests

#### InitializeRequest `initialize`
```text
✅  clientID?: string;
✅  clientName?: string;
✅  adapterID: string;
❌  locale?: string;
❌  linesStartAt1?: boolean;
❌  columnsStartAt1?: boolean;
❌  pathFormat?: 'path' | 'uri' | string;
❌  supportsVariableType?: boolean;
❌  supportsVariablePaging?: boolean;
❌  supportsRunInTerminalRequest?: boolean;
❌  supportsMemoryReferences?: boolean;
❌  supportsProgressReporting?: boolean;
❌  supportsInvalidatedEvent?: boolean;
❌  supportsMemoryEvent?: boolean;
❌  supportsArgsCanBeInterpretedByShell?: boolean;
❌  supportsStartDebuggingRequest?: boolean;
❌  supportsANSIStyling?: boolean;
```
#### InitializeResponse
```text
✅  body?: Capabilities;
```
#### LaunchRequest `launch`
```text
❌  noDebug?: boolean;
❌  __restart?: any;
🧩  cwd?: string;
🧩  env?: { [key: string]: string; };
🧩  program?: string;
🧩  args?: string;
🧩  stopAtEntry?: boolean;
🧩  justMyCode?: boolean;
🧩  enableStepFiltering?: boolean;
🧩  expressionEvaluationOptions?: ExpressionEvaluationOptions;
🧩  console?: 'internalConsole' | 'remoteConsole' | 'externalTerminal';
🧩  suppressJITOptimizations?: boolean;
```
#### LaunchResponse
```text
⚪  no additional fields
```
#### AttachRequest `attach`
```text
❌  __restart?: any;
✅  processId: number;
```
#### AttachResponse
```text
⚪  no additional fields
```
#### DisconnectRequest `disconnect`
```text
❌  restart?: boolean;
✅  terminateDebuggee?: boolean;
❌  suspendDebuggee?: boolean;
```
#### DisconnectResponse
```text
⚪  no additional fields
```
#### TerminateRequest `terminate`
```text
❌  restart?: boolean;
```
#### TerminateResponse
```text
⚪  no additional fields
```
#### BreakpointLocationsRequest `breakpointLocations`
```text
✅  source: Source;
✅  line: number;
✅  column?: number;
✅  endLine?: number;
✅  endColumn?: number;
```
#### BreakpointLocationsResponse
```text
✅  breakpoints: BreakpointLocation[];
```
#### SetBreakpointsRequest `setBreakpoints`
```text
✅  source: Source;
✅  breakpoints?: SourceBreakpoint[];
❌  lines?: number[];
❌  sourceModified?: boolean;
```
#### SetBreakpointsResponse
```text
✅  breakpoints: Breakpoint[];
```
#### SetFunctionBreakpointsRequest `setFunctionBreakpoints`
```text
✅  breakpoints: FunctionBreakpoint[];
```
#### SetFunctionBreakpointsResponse
```text
✅  breakpoints: Breakpoint[];
```
#### SetExceptionBreakpointsRequest `setExceptionBreakpoints`
```text
✅  filters: string[];
✅  filterOptions?: ExceptionFilterOptions[];
❌  exceptionOptions?: ExceptionOptions[];
```
#### SetExceptionBreakpointsResponse
```text
✅  breakpoints?: Breakpoint[];
```
#### ConfigurationDoneRequest `configurationDone`
```text
⚪  no additional fields
```
#### ContinueRequest `continue`
```text
✅  threadId: number;
✅  singleThread?: boolean;
```
#### ContinueResponse
```text
✅  allThreadsContinued?: boolean;
🧩  threadId: number;
```
#### NextRequest `next`
```text
✅  threadId: number;
✅  singleThread?: boolean;
❌  granularity?: SteppingGranularity;
```
#### NextResponse
```text
⚪  no additional fields
```
#### StepInRequest `stepIn`
```text
✅  threadId: number;
✅  singleThread?: boolean;
❌  targetId?: number;
❌  granularity?: SteppingGranularity;
```
#### StepInResponse
```text
⚪  no additional fields
```
#### StepOutRequest `stepOut`
```text
✅  threadId: number;
✅  singleThread?: boolean;
❌  granularity?: SteppingGranularity;
```
#### StepOutResponse
```text
⚪  no additional fields
```
#### PauseRequest `pause`
```text
✅  threadId: number;
```
#### PauseResponse
```text
⚪  no additional fields
```
#### GotoRequest `goto`
```text
✅  threadId: number;
✅  targetId: number;
```
#### GotoResponse
```text
⚪  no additional fields
```
#### StackTraceRequest `stackTrace`
```text
✅  threadId: number;
✅  startFrame?: number;
✅  levels?: number;
❌  format?: StackFrameFormat;
```
#### StackTraceResponse
```text
✅  stackFrames: StackFrame[];
✅  totalFrames?: number;
```
#### ScopesRequest `scopes`
```text
✅  frameId: number;
```
#### ScopesResponse
```text
✅  scopes: Scope[];
```
#### VariablesRequest `variables`
```text
✅  variablesReference: number;
❌  filter?: 'indexed' | 'named';
❌  start?: number;
❌  count?: number;
❌  format?: ValueFormat;
```
#### VariablesResponse
```text
✅  variables: Variable[];
```
#### SetVariableRequest `setVariable`
```text
✅  variablesReference: number;
✅  name: string;
✅  value: string;
❌  format?: ValueFormat;
```
#### SetVariableResponse
```text
✅  value: string;
❌  type?: string;
❌  variablesReference?: number;
❌  namedVariables?: number;
❌  indexedVariables?: number;
❌  memoryReference?: string;
❌  valueLocationReference?: number;
```
#### SourceRequest `source`
```text
✅  source?: Source;
✅  sourceReference: number;
```
#### SourceResponse
```text
✅  content: string;
❌  mimeType?: string;
```
#### ThreadsRequest `threads`
```text
⚪  no additional fields
```
#### ThreadsResponse
```text
✅  threads: Thread[];
```
#### ModulesRequest `modules`
```text
✅  startModule?: number;
✅  moduleCount?: number;
```
#### ModulesResponse
```text
✅  modules: Module[];
✅  totalModules?: number;
```
#### LoadedSourcesRequest `loadedSources`
```text
⚪  no additional fields
```
#### LoadedSourcesResponse
```text
✅  sources: Source[];
```
#### EvaluateRequest `evaluate`
```text
✅  expression: string;
✅  frameId?: number;
❌  line?: number;
❌  column?: number;
❌  source?: Source;
❌  context?: 'watch' | 'repl' | 'hover' | 'clipboard' | 'variables' | string;
❌  format?: ValueFormat;
```
#### EvaluateResponse
```text
✅  result: string;
✅  type?: string;
❌  presentationHint?: VariablePresentationHint;
✅  variablesReference: number;
❌  namedVariables?: number;
❌  indexedVariables?: number;
✅  memoryReference?: string;
❌  valueLocationReference?: number;
```
#### SetExpressionRequest `setExpression`
```text
✅  expression: string;
✅  value: string;
✅  frameId?: number;
❌  format?: ValueFormat;
```
#### SetExpressionResponse
```text
✅  value: string;
❌  type?: string;
❌  presentationHint?: VariablePresentationHint;
❌  variablesReference?: number;
❌  namedVariables?: number;
❌  indexedVariables?: number;
❌  memoryReference?: string;
❌  valueLocationReference?: number;
```
#### GotoTargetsRequest `gotoTargets`
```text
✅  source: Source;
✅  line: number;
✅  column?: number;
```
#### GotoTargetsResponse
```text
✅  targets: GotoTarget[];
```
#### ExceptionInfoRequest `exceptionInfo`
```text
✅  threadId: number;
```
#### ExceptionInfoResponse
```text
✅  exceptionId: string;
✅  description?: string;
✅  breakMode: ExceptionBreakMode;
✅  details?: ExceptionDetails;
```

## Types

#### Capabilities
```text
✅  supportsConfigurationDoneRequest?: boolean;
✅  supportsFunctionBreakpoints?: boolean;
✅  supportsConditionalBreakpoints?: boolean;
✅  supportsHitConditionalBreakpoints?: boolean;
❌  supportsEvaluateForHovers?: boolean;
✅  exceptionBreakpointFilters?: ExceptionBreakpointsFilter[];
❌  supportsStepBack?: boolean;
✅  supportsSetVariable?: boolean;
❌  supportsRestartFrame?: boolean;
✅  supportsGotoTargetsRequest?: boolean;
❌  supportsStepInTargetsRequest?: boolean;
❌  supportsCompletionsRequest?: boolean;
❌  completionTriggerCharacters?: string[];
❌  supportsModulesRequest?: boolean;
❌  additionalModuleColumns?: ColumnDescriptor[];
❌  supportedChecksumAlgorithms?: ChecksumAlgorithm[];
❌  supportsRestartRequest?: boolean;
✅  supportsExceptionOptions?: boolean;
❌  supportsValueFormattingOptions?: boolean;
✅  supportsExceptionInfoRequest?: boolean;
✅  supportTerminateDebuggee?: boolean;
❌  supportSuspendDebuggee?: boolean;
❌  supportsDelayedStackTraceLoading?: boolean;
✅  supportsLoadedSourcesRequest?: boolean;
✅  supportsLogPoints?: boolean;
❌  supportsTerminateThreadsRequest?: boolean;
✅  supportsSetExpression?: boolean;
✅  supportsTerminateRequest?: boolean;
❌  supportsDataBreakpoints?: boolean;
❌  supportsReadMemoryRequest?: boolean;
❌  supportsWriteMemoryRequest?: boolean;
❌  supportsDisassembleRequest?: boolean;
✅  supportsCancelRequest?: boolean;
✅  supportsBreakpointLocationsRequest?: boolean;
❌  supportsClipboardContext?: boolean;
❌  supportsSteppingGranularity?: boolean;
❌  supportsInstructionBreakpoints?: boolean;
✅  supportsExceptionFilterOptions?: boolean;
✅  supportsSingleThreadExecutionRequests?: boolean;
❌  supportsDataBreakpointBytes?: boolean;
❌  breakpointModes?: BreakpointMode[];
❌  supportsANSIStyling?: boolean;
```
#### Checksum
```text
✅  algorithm: ChecksumAlgorithm;
✅  checksum: string;
```
#### ExceptionBreakpointsFilter
```text
✅  filter: string;
✅  label: string;
❌  description?: string;
❌  default?: boolean;
❌  supportsCondition?: boolean;
❌  conditionDescription?: string;
```
#### Module
```text
✅  id: number | string;
✅  name: string;
✅  path?: string;
✅  isOptimized?: boolean;
✅  isUserCode?: boolean;
❌  version?: string;
✅  symbolStatus?: string;
✅  symbolFilePath?: string;
❌  dateTimeStamp?: string;
✅  addressRange?: string;
```
#### Thread
```text
✅  id: number;
✅  name: string;
```
#### Source
```text
✅  name?: string;
✅  path?: string;
✅  sourceReference?: number;
❌  presentationHint?: 'normal' | 'emphasize' | 'deemphasize';
❌  origin?: string;
❌  sources?: Source[];
❌  adapterData?: any;
✅  checksums?: Checksum[];
```
#### StackFrame
```text
✅  id: number;
✅  name: string;
✅  source?: Source;
✅  line: number;
✅  column: number;
✅  endLine?: number;
✅  endColumn?: number;
❌  canRestart?: boolean;
✅  instructionPointerReference?: string;
✅  moduleId?: number | string;
✅  presentationHint?: 'normal' | 'label' | 'subtle';
```
#### Scope
```text
✅  name: string;
❌  presentationHint?: 'arguments' | 'locals' | 'registers' | 'returnValue' | string;
✅  variablesReference: number;
❌  namedVariables?: number;
❌  indexedVariables?: number;
✅  expensive: boolean;
❌  source?: Source;
❌  line?: number;
❌  column?: number;
❌  endLine?: number;
❌  endColumn?: number;
```
#### Variable
```text
✅  name: string;
✅  value: string;
✅  type?: string;
❌  presentationHint?: VariablePresentationHint;
✅  evaluateName?: string;
✅  variablesReference: number;
❌  namedVariables?: number;
❌  indexedVariables?: number;
✅  memoryReference?: string;
❌  declarationLocationReference?: number;
❌  valueLocationReference?: number;
```
#### SourceBreakpoint
```text
✅  line: number;
✅  column?: number;
✅  condition?: string;
✅  hitCondition?: string;
✅  logMessage?: string;
❌  mode?: string;
```
#### FunctionBreakpoint
```text
✅  name: string;
✅  condition?: string;
✅  hitCondition?: string;
```
#### Breakpoint
```text
✅  id?: number;
✅  verified: boolean;
✅  message?: string;
✅  source?: Source;
✅  line?: number;
✅  column?: number;
✅  endLine?: number;
✅  endColumn?: number;
✅  instructionReference?: string;
✅  offset?: number;
❌  reason?: 'pending' | 'failed';
```
#### BreakpointLocation
```text
✅  line: number;
✅  column?: number;
✅  endLine?: number;
✅  endColumn?: number;
```
#### GotoTarget
```text
✅  id: number;
✅  label: string;
✅  line: number;
✅  column?: number;
✅  endLine?: number;
✅  endColumn?: number;
✅  instructionPointerReference?: string;
```
#### ExceptionFilterOptions
```text
✅  filterId: string;
✅  condition?: string;
❌  mode?: string;
```
#### ExceptionOptions
```text
❌  path?: ExceptionPathSegment[];
✅  breakMode: ExceptionBreakMode;
```
#### ExceptionDetails
```text
✅  message?: string;
✅  typeName?: string;
✅  fullTypeName?: string;
✅  evaluateName?: string;
✅  stackTrace?: string;
✅  innerException?: ExceptionDetails[];
🧩  formattedDescription?: string;
🧩  source?: string;
```
#### ExpressionEvaluationOptions
```text
🧩  allowImplicitFuncEval?: boolean;
🧩  allowToString?: boolean;
🧩  showRawValues?: boolean;
```
