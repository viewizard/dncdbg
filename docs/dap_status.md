## Debug Adapter Protocol support status.

#### Key to Notation
```diff
+ Implemented
- Not implemented
! Partially implemented
@@ Comments @@
```

#### Base Protocol

[ProtocolMessage](#protocolmessage), [Request](#request), [Event](#event), [Response](#response), [Cancel Request](#cancelrequest-cancel)

#### Events

[Stopped Event](#stoppedevent), [Continued Event](#continuedevent), [Exited Event](#exitedevent), [Terminated Event](#terminatedevent), [Thread Event](#threadevent), [Output Event](#outputevent), [Breakpoint Event](#breakpointevent), [Module Event](#moduleevent), [Process Event](#processevent), [Capabilities Event](#capabilitiesevent)

#### Requests

[Initialize Request](#initializerequest-initialize), [Launch Request](#launchrequest-launch), [Attach Request](#attachrequest-attach), [Disconnect Request](#disconnectrequest-disconnect), [Terminate Request](#terminaterequest-terminate), [SetBreakpoints Request](#setbreakpointsrequest-setbreakpoints), [SetFunctionBreakpoints Request](#setfunctionbreakpointsrequest-setfunctionbreakpoints), [SetExceptionBreakpoints Request](#setexceptionbreakpointsrequest-setexceptionbreakpoints), [Continue Request](#continuerequest-continue), [Next Request](#nextrequest-next), [StepIn Request](#stepinrequest-stepin), [StepOut Request](#stepoutrequest-stepout), [Pause Request](#pauserequest-pause), [StackTrace Request](#stacktracerequest-stacktrace), [Scopes Request](#scopesrequest-scopes), [Variables Request](#variablesrequest-variables)
[SetVariable Request](#setvariablerequest-setvariable), [Threads Request](#threadsrequest-threads), [Evaluate Request](#evaluaterequest-evaluate), [SetExpression Request](#setexpressionrequest-setexpression), [ExceptionInfo Request](#exceptioninforequest-exceptioninfo)

#### Types

[Capabilities](#capabilities), [ExceptionBreakpointsFilter](#exceptionbreakpointsfilter), [Message](#message), [Module](#module), [Thread](#thread), [Source](#source), [StackFrame](#stackframe), [Scope](#scope), [Variable](#variable), [SourceBreakpoint](#sourcebreakpoint), [FunctionBreakpoint](#functionbreakpoint), [Breakpoint](#breakpoint), [ExceptionFilterOptions](#exceptionfilteroptions), [ExceptionOptions](#exceptionoptions), [ExceptionDetails](#exceptiondetails), [ExpressionEvaluationOptions](#expressionevaluationoptions)

## Base Protocol

#### ProtocolMessage
```diff
+  seq: number;
+  type: 'request' | 'response' | 'event' | string;
```
#### Request
```diff
+  command: string;
+  arguments?: any;
```
#### Event
```diff
+  event: string;
+  body?: any;
```
#### Response
```diff
+  request_seq: number;
+  success: boolean;
+  command: string;
-  message?: 'cancelled' | 'notStopped' | string;
+  body?: any;
```
#### ErrorResponse
```diff
-  error?: Message;
```
#### CancelRequest `cancel`
```diff
+   requestId?: number;
+   progressId?: string;
```
#### CancelResponse
```diff
```
#### StoppedEvent
```diff
+   reason: 'step' | 'breakpoint' | 'exception' | 'pause' | 'entry' | 'function breakpoint'
-       | 'goto' | 'data breakpoint' | 'instruction breakpoint'
+       | string;
-   description?: string;
+   threadId?: number;
-   preserveFocusHint?: boolean;
+   text?: string;
+   allThreadsStopped?: boolean;
!   hitBreakpointIds?: number[];
@@ hitBreakpointIds provide only one breakpoint now, first found for this code line @@
```
#### ContinuedEvent
```diff
+   threadId: number;
+   allThreadsContinued?: boolean;
```
#### ExitedEvent
```diff
+   exitCode: number;
```
#### TerminatedEvent
```diff
-   restart?: any;
```
#### ThreadEvent
```diff
+   reason: 'started' | 'exited' | string;
+   threadId: number;
```
#### OutputEvent
```diff
+   category?: 'console' | 'stdout' | 'stderr'
-              | 'important' | 'telemetry' 
+              | string;
+   output: string;
-   group?: 'start' | 'startCollapsed' | 'end';
-   variablesReference?: number;
+   source?: Source;
-   line?: number;
-   column?: number;
-   data?: any;
-   locationReference?: number;
```
#### BreakpointEvent
```diff
+   reason: 'changed' | 'new' | 'removed' | string;
+   breakpoint: Breakpoint;
```
#### ModuleEvent
```diff
+   reason: 'new' | 'changed' | 'removed';
+   module: Module;
```
#### ProcessEvent
```diff
+   name: string;
+   systemProcessId?: number;
+   isLocalProcess?: boolean;
+   startMethod?: 'launch' 
-               | 'attach' | 'attachForSuspendedLaunch';
-   pointerSize?: number;
```
#### CapabilitiesEvent
```diff
+   capabilities: Capabilities;
```

## Requests

#### InitializeRequest `initialize`
```diff
+   clientID?: string;
+   clientName?: string;
+   adapterID: string;
-   locale?: string;
-   linesStartAt1?: boolean;
-   columnsStartAt1?: boolean;
-   pathFormat?: 'path' | 'uri' | string;
-   supportsVariableType?: boolean;
-   supportsVariablePaging?: boolean;
-   supportsRunInTerminalRequest?: boolean;
-   supportsMemoryReferences?: boolean;
-   supportsProgressReporting?: boolean;
-   supportsInvalidatedEvent?: boolean;
-   supportsMemoryEvent?: boolean;
-   supportsArgsCanBeInterpretedByShell?: boolean;
-   supportsStartDebuggingRequest?: boolean;
-   supportsANSIStyling?: boolean;
```
#### InitializeResponse
```diff
+   body?: Capabilities;
```
#### LaunchRequest `launch`
```diff
-   noDebug?: boolean;
-   __restart?: any;
@@ VSCode IDE additional fields: @@
+   cwd?: string;
+   env?: string;
+   program?: string;
+   args?: string;
+   stopAtEntry?: boolean;
+   justMyCode?: boolean;
+   enableStepFiltering?: boolean;
+   expressionEvaluationOptions?: ExpressionEvaluationOptions;
```
#### LaunchResponse
```diff
```
#### AttachRequest `attach`
```diff
-   __restart?: any;
```
#### AttachResponse
```diff
```
#### DisconnectRequest `disconnect`
```diff
-   restart?: boolean;
+   terminateDebuggee?: boolean;
-   suspendDebuggee?: boolean;
```
#### DisconnectResponse
```diff
```
#### TerminateRequest `terminate`
```diff
-   restart?: boolean;
```
#### TerminateResponse
```diff
```
#### SetBreakpointsRequest `setBreakpoints`
```diff
+   source: Source;
+   breakpoints?: SourceBreakpoint[];
-   lines?: number[];
-   sourceModified?: boolean;
```
#### SetBreakpointsResponse
```diff
+   breakpoints: Breakpoint[];
```
#### SetFunctionBreakpointsRequest `setFunctionBreakpoints`
```diff
+   breakpoints: FunctionBreakpoint[];
```
#### SetFunctionBreakpointsResponse
```diff
+   breakpoints: Breakpoint[];
```
#### SetExceptionBreakpointsRequest `setExceptionBreakpoints`
```diff
+   filters: string[];
+   filterOptions?: ExceptionFilterOptions[];
-   exceptionOptions?: ExceptionOptions[];
```
#### SetExceptionBreakpointsResponse
```diff
+   breakpoints?: Breakpoint[];
```
#### ContinueRequest `continue`
```diff
+   threadId: number;
-   singleThread?: boolean;
```
#### ContinueResponse 
```diff
+   allThreadsContinued?: boolean;
@@ VSCode IDE additional field: @@
+   threadId: number;
```
#### NextRequest `next`
```diff
+   threadId: number;
-   singleThread?: boolean;
-   granularity?: SteppingGranularity;
```
#### NextResponse
```diff
```
#### StepInRequest `stepIn`
```diff
+   threadId: number;
-   singleThread?: boolean;
-   targetId?: number;
-   granularity?: SteppingGranularity;
```
#### StepInResponse
```diff
```
#### StepOutRequest `stepOut`
```diff
+   threadId: number;
-   singleThread?: boolean;
-   granularity?: SteppingGranularity;
```
#### StepOutResponse
```diff
```
#### PauseRequest `pause`
```diff
+   threadId: number;
```
#### PauseResponse
```diff
```
#### StackTraceRequest `stackTrace`
```diff
+   threadId: number;
+   startFrame?: number;
+   levels?: number;
-   format?: StackFrameFormat;
```
#### StackTraceResponse
```diff
+   stackFrames: StackFrame[];
+   totalFrames?: number;
```
#### ScopesRequest `scopes`
```diff
+   frameId: number;
```
#### ScopesResponse
```diff
+   scopes: Scope[];
```
#### VariablesRequest `variables`
```diff
+   variablesReference: number;
+   filter?: 'indexed' | 'named';
+   start?: number;
+   count?: number;
-   format?: ValueFormat;
```
#### VariablesResponse
```diff
+   variables: Variable[];
```
#### SetVariableRequest `setVariable`
```diff
+   variablesReference: number;
+   name: string;
+   value: string;
-   format?: ValueFormat;
```
#### SetVariableResponse
```diff
+   value: string;
-   type?: string;
-   variablesReference?: number;
-   namedVariables?: number;
-   indexedVariables?: number;
-   memoryReference?: string;
-   valueLocationReference?: number;
```
#### ThreadsRequest `threads`
```diff
```
#### ThreadsResponse
```diff
+   threads: Thread[];
```
#### EvaluateRequest `evaluate`
```diff
+   expression: string;
+   frameId?: number;
-   line?: number;
-   column?: number;
-   source?: Source;
-   context?: 'watch' | 'repl' | 'hover' | 'clipboard' | 'variables' | string;
-   format?: ValueFormat;
```
#### EvaluateResponse
```diff
+   result: string;
+   type?: string;
-   presentationHint?: VariablePresentationHint;
+   variablesReference: number;
+   namedVariables?: number;
-   indexedVariables?: number;
-   memoryReference?: string;
-   valueLocationReference?: number;
```
#### SetExpressionRequest `setExpression`
```diff
+   expression: string;
+   value: string;
+   frameId?: number;
-   format?: ValueFormat;
```
#### SetExpressionResponse
```diff
+   value: string;
-   type?: string;
-   presentationHint?: VariablePresentationHint;
-   variablesReference?: number;
-   namedVariables?: number;
-   indexedVariables?: number;
-   memoryReference?: string;
-   valueLocationReference?: number;
```
#### ExceptionInfoRequest `exceptionInfo`
```diff
+   threadId: number;
```
#### ExceptionInfoResponse
```diff
+   exceptionId: string;
+   description?: string;
+   breakMode: ExceptionBreakMode;
+   details?: ExceptionDetails;
```

## Types

#### Capabilities
```diff
+   supportsConfigurationDoneRequest?: boolean;
+   supportsFunctionBreakpoints?: boolean;
+   supportsConditionalBreakpoints?: boolean;
-   supportsHitConditionalBreakpoints?: boolean;
-   supportsEvaluateForHovers?: boolean;
+   exceptionBreakpointFilters?: ExceptionBreakpointsFilter[];
-   supportsStepBack?: boolean;
+   supportsSetVariable?: boolean;
-   supportsRestartFrame?: boolean;
-   supportsGotoTargetsRequest?: boolean;
-   supportsStepInTargetsRequest?: boolean;
-   supportsCompletionsRequest?: boolean;
-   completionTriggerCharacters?: string[];
-   supportsModulesRequest?: boolean;
-   additionalModuleColumns?: ColumnDescriptor[];
-   supportedChecksumAlgorithms?: ChecksumAlgorithm[];
-   supportsRestartRequest?: boolean;
+   supportsExceptionOptions?: boolean;
-   supportsValueFormattingOptions?: boolean;
+   supportsExceptionInfoRequest?: boolean;
+   supportTerminateDebuggee?: boolean;
-   supportSuspendDebuggee?: boolean;
-   supportsDelayedStackTraceLoading?: boolean;
-   supportsLoadedSourcesRequest?: boolean;
-   supportsLogPoints?: boolean;
-   supportsTerminateThreadsRequest?: boolean;
+   supportsSetExpression?: boolean;
+   supportsTerminateRequest?: boolean;
-   supportsDataBreakpoints?: boolean;
-   supportsReadMemoryRequest?: boolean;
-   supportsWriteMemoryRequest?: boolean;
-   supportsDisassembleRequest?: boolean;
+   supportsCancelRequest?: boolean;
-   supportsBreakpointLocationsRequest?: boolean;
-   supportsClipboardContext?: boolean;
-   supportsSteppingGranularity?: boolean;
-   supportsInstructionBreakpoints?: boolean;
+   supportsExceptionFilterOptions?: boolean;
-   supportsSingleThreadExecutionRequests?: boolean;
-   supportsDataBreakpointBytes?: boolean;
-   breakpointModes?: BreakpointMode[];
-   supportsANSIStyling?: boolean;
```
#### ExceptionBreakpointsFilter
```diff
+   filter: string;
+   label: string;
-   description?: string;
-   default?: boolean;
-   supportsCondition?: boolean;
-   conditionDescription?: string;
```
#### Message
```diff
+   id: number;
-   format: string;
-   variables?: { [key: string]: string; };
-   sendTelemetry?: boolean;
-   showUser?: boolean;
-   url?: string;
-   urlLabel?: string;
```
#### Module
```diff
+   id: number | string;
+   name: string;
+   path?: string;
-   isOptimized?: boolean;
-   isUserCode?: boolean;
-   version?: string;
+   symbolStatus?: string;
-   symbolFilePath?: string;
-   dateTimeStamp?: string;
-   addressRange?: string;
```
#### Thread
```diff
+   id: number;
+   name: string;
```
#### Source
```diff
+   name?: string;
+   path?: string;
-   sourceReference?: number;
-   presentationHint?: 'normal' | 'emphasize' | 'deemphasize';
-   origin?: string;
-   sources?: Source[];
-   adapterData?: any;
-   checksums?: Checksum[];
```
#### StackFrame
```diff
+   id: number;
+   name: string;
+   source?: Source;
+   line: number;
+   column: number;
+   endLine?: number;
+   endColumn?: number;
-   canRestart?: boolean;
-   instructionPointerReference?: string;
+   moduleId?: number | string;
-   presentationHint?: 'normal' | 'label' | 'subtle';
```
#### Scope
```diff
+   name: string;
-   presentationHint?: 'arguments' | 'locals' | 'registers' | 'returnValue' | string;
+   variablesReference: number;
+   namedVariables?: number;
+   indexedVariables?: number;
+   expensive: boolean;
-   source?: Source;
-   line?: number;
-   column?: number;
-   endLine?: number;
-   endColumn?: number;
```
#### Variable
```diff
+   name: string;
+   value: string;
+   type?: string;
-   presentationHint?: VariablePresentationHint;
+   evaluateName?: string;
+   variablesReference: number;
+   namedVariables?: number;
+   indexedVariables?: number;
-   memoryReference?: string;
-   declarationLocationReference?: number;
-   valueLocationReference?: number;
```
#### SourceBreakpoint
```diff
+   line: number;
+   column?: number;
+   condition?: string;
-   hitCondition?: string;
-   logMessage?: string;
-   mode?: string;
```
#### FunctionBreakpoint
```diff
+   name: string;
+   condition?: string;
-   hitCondition?: string;
```
#### Breakpoint
```diff
+   id?: number;
+   verified: boolean;
+   message?: string;
+   source?: Source;
+   line?: number;
-   column?: number;
+   endLine?: number;
-   endColumn?: number;
-   instructionReference?: string;
-   offset?: number;
-   reason?: 'pending' | 'failed';
```
#### ExceptionFilterOptions
```diff
+   filterId: string;
+   condition?: string;
-   mode?: string;
```
#### ExceptionOptions
```diff
-   path?: ExceptionPathSegment[];
+   breakMode: ExceptionBreakMode;
```
#### ExceptionDetails
```diff
+   message?: string;
+   typeName?: string;
+   fullTypeName?: string;
+   evaluateName?: string;
+   stackTrace?: string;
+   innerException?: ExceptionDetails[];
@@ VSCode IDE additional fields: @@
+   std::string formattedDescription;
+   std::string source;
```
#### ExpressionEvaluationOptions
```diff
@@ VSCode IDE additional field: @@
+   allowImplicitFuncEval?: boolean;
```
