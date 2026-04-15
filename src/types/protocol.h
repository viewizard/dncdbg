// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef TYPES_PROTOCOL_H
#define TYPES_PROTOCOL_H

#include "types/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace dncdbg
{

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_Thread
struct Thread
{
    ThreadId id;
    std::string name;

    Thread(ThreadId id_, std::string name_)
        : id(id_),
          name(std::move(name_))
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

    explicit Source(const std::string &path = {});

    [[nodiscard]] bool IsNull() const
    {
        return name.empty() && path.empty();
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
        : line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    StackFrame(ThreadId threadId,
               FrameLevel level_,
               const std::string &name_)
        : id(FrameId(threadId, level_)),
          name(name_),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    StackFrame(ThreadId threadId,
               FrameLevel level_,
               std::string &&name_)
        : id(FrameId(threadId, level_)),
          name(std::move(name_)),
          line(0),
          column(0),
          endLine(0),
          endColumn(0)
    {
    }

    explicit StackFrame(FrameId id)
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
    uint32_t id{0};
    bool verified{false};
    std::string message;
    Source source;
    int line{0};
    // column?: number;
    int endLine{0};
    // endColumn?: number;
    // instructionReference?: string;
    // offset?: number;
    // reason?: 'pending' | 'failed';
};

enum class SymbolStatus : uint8_t
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
    bool isOptimized{true};
    bool isUserCode{false};
    // version?: string;
    SymbolStatus symbolStatus{SymbolStatus::Skipped};
    std::string symbolFilePath;
    // dateTimeStamp?: string;
    std::string addressRange;
};

enum class StartMethod : uint8_t
{
    None,
    Launch,
    Attach
};

enum class StoppedEventReason : uint8_t
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
    std::vector<uint32_t> hitBreakpointIds;

    explicit StoppedEvent(StoppedEventReason reason,
                          ThreadId threadId = ThreadId::Invalid)
        : reason(reason),
          threadId(threadId),
          allThreadsStopped(true)
    {
    }

    StoppedEvent(StoppedEventReason reason,
                 std::vector<uint32_t> &&bpIds,
                 ThreadId threadId = ThreadId::Invalid)
        : reason(reason),
          threadId(threadId),
          allThreadsStopped(true),
          hitBreakpointIds(std::move(bpIds))
    {
    }
};

enum class BreakpointEventReason : uint8_t
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

    BreakpointEvent(const BreakpointEventReason &reason,
                    Breakpoint breakpoint)
        : reason(reason),
          breakpoint(std::move(breakpoint))
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Exited
struct ExitedEvent
{
    int exitCode;

    explicit ExitedEvent(int exitCode)
        : exitCode(exitCode)
    {
    }
};

enum class ThreadEventReason : uint8_t
{
    Started,
    Exited
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Thread
struct ThreadEvent
{
    ThreadEventReason reason;
    ThreadId threadId;

    ThreadEvent(ThreadEventReason reason_,
                ThreadId id_)
        : reason(reason_),
          threadId(id_)
    {
    }
};

enum class OutputCategory : uint8_t
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
    int line{0};
    int column{0};
    // data?: any;
    // locationReference?: number;

    OutputEvent(OutputCategory category_,
                std::string output_)
        : category(category_),
          output(std::move(output_))
    {
    }
};

enum class ModuleEventReason : uint8_t
{
    New,
    Changed,
    Removed
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Events_Module
struct ModuleEvent
{
    ModuleEventReason reason;
    const Module &module;

    ModuleEvent(ModuleEventReason reason,
                const Module &module)
        : reason(reason),
          module(module)
    {
    }

    ModuleEvent(ModuleEvent &&) = delete;
    ModuleEvent(const ModuleEvent &) = delete;
    ModuleEvent &operator=(ModuleEvent &&) = delete;
    ModuleEvent &operator=(const ModuleEvent &) = delete;
    ~ModuleEvent() = default;
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

    Scope(uint32_t variablesReference,
          std::string name,
          int namedVariables)
        : name(std::move(name)),
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
    uint32_t variablesReference{0};
    int namedVariables{0};
    int indexedVariables{0};
    // memoryReference?: string;
    // declarationLocationReference?: number;
    // valueLocationReference?: number;
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_SourceBreakpoint
struct SourceBreakpoint
{
    int line;
    // column?: number;
    std::string condition;
    std::string hitCondition;
    // logMessage?: string;
    // mode?: string;

    explicit SourceBreakpoint(int linenum,
                              std::string cond = std::string(),
                              std::string hitCond = std::string())
        : line(linenum),
          condition(std::move(cond)),
          hitCondition(std::move(hitCond))
    {
    }
};

// https://microsoft.github.io/debug-adapter-protocol/specification#Types_FunctionBreakpoint
struct FunctionBreakpoint
{
    std::string func;  // name -> func(params)
    std::string params;// name -> func(params)
    std::string condition;
    std::string hitCondition;

    FunctionBreakpoint(std::string func,
                       std::string params,
                       std::string cond = std::string(),
                       std::string hitCond = std::string())
        : func(std::move(func)),
          params(std::move(params)),
          condition(std::move(cond)),
          hitCondition(std::move(hitCond))
    {
    }
};

// Based on CorDebugExceptionCallbackType, but include info about JMC status in catch handler.
// https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/cordebugexceptioncallbacktype-enumeration
enum class ExceptionCallbackType : uint8_t
{
    UNKNOWN = 0,
    FIRST_CHANCE,
    USER_FIRST_CHANCE,
    CATCH_HANDLER_FOUND,
    USER_CATCH_HANDLER_FOUND,
    UNHANDLED
};

enum class ExceptionBreakMode : uint8_t
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
    // here, since exception object have only one exception object reference in InnerException field.
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

enum class ExceptionBreakpointFilter : uint8_t
{
    THROW                = 0,
    USER_UNHANDLED       = 1,
    THROW_USER_UNHANDLED = 2,
    UNHANDLED            = 3,
    Size                 = 4
};

enum class ExceptionCategory : uint8_t
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
    bool negativeCondition{false};

    ExceptionBreakpoint(ExceptionCategory category,
                        ExceptionBreakpointFilter filterId)
        : categoryHint(category),
          filterId(filterId)
    {
    }

    bool operator==(ExceptionBreakpointFilter id) const
    {
        return filterId == id;
    }
};

} // namespace dncdbg

#endif // TYPES_PROTOCOL_H
