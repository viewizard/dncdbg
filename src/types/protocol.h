// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include "types/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dncdbg
{

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Thread
struct Thread
{
    ThreadId id;
    std::string name;

    Thread(ThreadId id_, const std::string &name_, bool running_)
        : id(id_),
          name(name_)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Source
struct Source
{
    std::string name;
    std::string path;
    // sourceReference?: number;
    // presentationHint?: 'normal' | 'emphasize' | 'deemphasize';
    // origin?: string;
    // sources?: Source[];
    // adapterData?: any;
    // checksums?: Checksum[];

    Source(const std::string &path = {}) 
      : name(GetFileName(path)),
        path(path)
    {
    }
    bool IsNull() const
    {
        return name.empty() && path.empty();
    }

private:

    std::string GetFileName(const std::string &path)
    {
        const std::size_t i = path.find_last_of("/\\");
        return i == std::string::npos ? path : path.substr(i + 1);
    }

};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_StackFrame
struct StackFrame
{
    FrameId id;
    std::string name;
    Source source;
    int line;
    int column;
    int endLine;
    int endColumn;
    // canRestart?: boolean;
    // instructionPointerReference?: string;
    std::string moduleId;
    // presentationHint?: 'normal' | 'label' | 'subtle';

    StackFrame()
        : id(),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    StackFrame(ThreadId threadId, FrameLevel level_, const std::string &name_)
        : id(FrameId(threadId, level_)),
          name(name_),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    StackFrame(ThreadId threadId, FrameLevel level_, std::string &&name_)
        : id(FrameId(threadId, level_)),
          name(name_),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    StackFrame(FrameId id)
        : id(id),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Breakpoint
struct Breakpoint
{
    uint32_t id;
    bool verified;
    std::string message;
    Source source;
    int line;
    // column?: number;
    int endLine;
    // endColumn?: number;

    // instructionReference?: string;
    // offset?: number;
    // reason?: 'pending' | 'failed';

    Breakpoint()
        : id(0),
          verified(false),
          line(0),
          endLine(0)
    {
    }
};

enum class SymbolStatus
{
    Skipped, // "Skipped loading symbols."
    Loaded,  // "Symbols loaded."
    NotFound
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Module
struct Module
{
    std::string id;
    std::string name;
    std::string path;
    // isOptimized?: boolean;
    // isUserCode?: boolean;
    // version?: string;
    SymbolStatus symbolStatus;
    // symbolFilePath?: string;
    // dateTimeStamp?: string;
    // addressRange?: string;

    Module() : symbolStatus(SymbolStatus::Skipped)
    {
    }
};

enum class StoppedEventReason
{
    Step,
    Breakpoint,
    Exception,
    Pause,
    Entry
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Stopped
struct StoppedEvent
{
    StoppedEventReason reason;
    // description?: string;
    ThreadId threadId;
    // preserveFocusHint?: boolean;
    std::string text;
    bool allThreadsStopped;
    // hitBreakpointIds?: number[];

    StoppedEvent(StoppedEventReason reason, ThreadId threadId = ThreadId::Invalid)
        : reason(reason),
          threadId(threadId),
          allThreadsStopped(true)
    {
    }
};

enum class BreakpointEventReason
{
    Changed,
    New,
    Removed
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Breakpoint
struct BreakpointEvent
{
    BreakpointEventReason reason;
    Breakpoint breakpoint;

    BreakpointEvent(const BreakpointEventReason &reason, const Breakpoint &breakpoint)
        : reason(reason),
          breakpoint(breakpoint)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Exited
struct ExitedEvent
{
    int exitCode;

    ExitedEvent(int exitCode) : exitCode(exitCode)
    {
    }
};

enum class ThreadEventReason
{
    Started,
    Exited
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Thread
struct ThreadEvent
{
    ThreadEventReason reason;
    ThreadId threadId;

    ThreadEvent(ThreadEventReason reason_, ThreadId id_)
        : reason(reason_),
          threadId(id_)
    {
    }
};

enum class OutputCategory
{
    Console,
    StdOut,
    StdErr
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Output
struct OutputEvent
{
    OutputCategory category;
    std::string output;
    // group?: 'start' | 'startCollapsed' | 'end';
    // variablesReference?: number;
    Source source;
    int line;
    int column;
    // data?: any;
    // locationReference?: number;

    OutputEvent(OutputCategory category_, const std::string &output_)
        : category(category_),
          output(output_),
          source(),
          line(0),
          column(0)
    {
    }
};

enum class ModuleEventReason
{
    New,
    Changed,
    Removed
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Module
struct ModuleEvent
{
    ModuleEventReason reason;
    Module module;

    ModuleEvent(ModuleEventReason reason, const Module &module)
        : reason(reason),
          module(module)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Scope
struct Scope
{
    std::string name;
    // presentationHint?: 'arguments' | 'locals' | 'registers' | 'returnValue' | string;
    uint32_t variablesReference;
    int namedVariables;
    int indexedVariables;
    bool expensive;
    // source?: Source;
    // line?: number;
    // column?: number;
    // endLine?: number;
    // endColumn?: number;

    Scope()
        : variablesReference(0),
          namedVariables(0),
          indexedVariables(0),
          expensive(false)
    {
    }

    Scope(uint32_t variablesReference, const std::string &name, int namedVariables)
        : name(name),
          variablesReference(variablesReference),
          namedVariables(namedVariables),
          indexedVariables(0),
          expensive(false)
    {
    }
};

// TODO: Replace strings with enums
struct VariablePresentationHint
{
    std::string kind;
    std::vector<std::string> attributes;
    std::string visibility;
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Variable
struct Variable
{
    std::string name;
    std::string value;
    std::string type;
    VariablePresentationHint presentationHint;
    std::string evaluateName;
    uint32_t variablesReference;
    int namedVariables;
    int indexedVariables;
    // memoryReference?: string;
    // declarationLocationReference?: number;
    // valueLocationReference?: number;

    Variable()
        : variablesReference(0),
          namedVariables(0),
          indexedVariables(0)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_SourceBreakpoint
struct SourceBreakpoint
{
    int line;
    // column?: number;
    std::string condition;
    // hitCondition?: string;
    // logMessage?: string;
    // mode?: string;

    SourceBreakpoint(int linenum, const std::string &cond = std::string())
        : line(linenum),
          condition(cond)
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_FunctionBreakpoint
struct FunctionBreakpoint
{
    std::string func;  // name -> func(params)
    std::string params;// name -> func(params)
    std::string condition;
    // hitCondition?: string;

    FunctionBreakpoint(const std::string &func, const std::string &params,
                   const std::string &cond = std::string())
        : func(func),
          params(params),
          condition(cond)
    {
    }
};

// Based on CorDebugExceptionCallbackType, but include info about JMC status in catch handler.
// https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/cordebugexceptioncallbacktype-enumeration
enum class ExceptionCallbackType
{
    UNKNOWN = 0,
    FIRST_CHANCE,
    USER_FIRST_CHANCE,
    CATCH_HANDLER_FOUND,
    USER_CATCH_HANDLER_FOUND,
    UNHANDLED
};

enum class ExceptionBreakMode
{
    NEVER,          // never stopped on this exception
    THROW,          // stopped on throw
    USER_UNHANDLED, // stopped on user-unhandled
    UNHANDLED       // stopped on unhandled
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_ExceptionDetails
struct ExceptionDetails
{
    std::string message;
    std::string typeName;
    std::string fullTypeName;
    std::string evaluateName;
    std::string stackTrace;
    // Note, DAP have "innerException" field as array, but in real we don't have array with inner exceptions
    // here, since exception object have only one exeption object reference in InnerException field.
    std::unique_ptr<ExceptionDetails> innerException;

    // not part of DAP specification, but send by vsdbg in `exceptionInfo` response in VSCode IDE
    std::string formattedDescription;
    std::string source;
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ExceptionInfo
struct ExceptionInfo
{
    std::string exceptionId;
    std::string description;
    std::string breakMode;
    ExceptionDetails details;
};

enum class ExceptionBreakpointFilter : size_t
{
    THROW                = 0,
    USER_UNHANDLED       = 1,
    THROW_USER_UNHANDLED = 2,
    UNHANDLED            = 3,
    Size                 = 4
};

enum class ExceptionCategory
{
    CLR,
    MDA,
    ANY
};

struct ExceptionBreakpoint
{
    ExceptionCategory categoryHint;
    ExceptionBreakpointFilter filterId;
    std::unordered_set<std::string> condition; // Note, only exception type related conditions allowed for now.
    bool negativeCondition;

    ExceptionBreakpoint(ExceptionCategory category, ExceptionBreakpointFilter filterId)
        : categoryHint(category),
          filterId(filterId),
          negativeCondition(false)
    {
    }

    bool operator==(ExceptionBreakpointFilter id) const
    {
        return filterId == id;
    }
};

} // namespace dncdbg
