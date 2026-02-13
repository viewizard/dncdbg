// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <wtypes.h>
#endif

#include <cassert>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace dncdbg
{

// This is helper class, which simplifies creation of custom scalar types
// (ones, which provide stron typing and disallow mixing with any other scalar types).
// Basically these types support equality compare operators and operator<
// (to allow using such types with STL containers).
//
template <typename T> struct CustomScalarType
{
    friend bool operator==(T a, T b)
    {
        return static_cast<typename T::ScalarType>(a) == static_cast<typename T::ScalarType>(b);
    }
    template <typename U> friend bool operator==(T a, U b)
    {
        return static_cast<typename T::ScalarType>(a) == b;
    }
    template <typename U> friend bool operator==(U a, T b)
    {
        return a == static_cast<typename T::ScalarType>(b);
    }
    friend bool operator!=(T a, T b)
    {
        return !(a == b);
    }
    template <typename U> friend bool operator!=(T a, U b)
    {
        return !(a == b);
    }
    template <typename U> friend bool operator!=(U a, T b)
    {
        return !(a == b);
    }

    bool operator<(const T &other) const
    {
        return static_cast<typename T::ScalarType>(static_cast<const T &>(*this)) <
               static_cast<typename T::ScalarType>(static_cast<const T &>(other));
    }
};

// Process identifier.
class PID : public CustomScalarType<PID>
{
  public:

    using ScalarType = DWORD;

    explicit PID(ScalarType n) : m_pid{static_cast<int>(n)}
    {
    }
    explicit operator ScalarType() const
    {
        return m_pid;
    }

  private:

    int m_pid;
};

// Data type dedicated to carry thread id.
class ThreadId : public CustomScalarType<ThreadId>
{
  public:

    using ScalarType = int;

    // This is for cases, when ThreadId isn't initialized/unknown.
    static const ThreadId Invalid;

    // This should be used as Any/All threads sign for protocol (for Emit...Event).
    static const ThreadId AllThreads;

    ThreadId() : ThreadId(Invalid)
    {
    }

    explicit ThreadId(int threadId) : m_id(threadId)
    {
        assert(threadId != InvalidValue && threadId != AllThreadsValue);
    }

    explicit ThreadId(DWORD threadId) : ThreadId(static_cast<int>(threadId))
    {
    }

    explicit operator bool() const
    {
        return m_id != InvalidValue;
    }

    explicit operator ScalarType() const
    {
        return assert(*this), m_id;
    }

  private:

    enum SpecialValues
    {
        InvalidValue = 0,
        AllThreadsValue = -1
    };

    int m_id;

    ThreadId(SpecialValues val)
        : m_id(val)
    {
    }
};

// Data type dedicated to carry stack frame depth (level).
class FrameLevel : public CustomScalarType<FrameLevel>
{
  public:

    using ScalarType = int;

    static const int MaxFrameLevel = SHRT_MAX;

    FrameLevel() : m_level(-1)
    {
    }

    explicit FrameLevel(unsigned n) : m_level{(assert(static_cast<int>(n) <= MaxFrameLevel), static_cast<int>(n))}
    {
    }
    explicit FrameLevel(int n) : FrameLevel(static_cast<unsigned>(n))
    {
    }

    explicit operator ScalarType() const
    {
        return assert(*this), m_level;
    }
    explicit operator bool() const
    {
        return m_level != -1;
    }

  private:

    int m_level;
};

// Unique stack frame identifier, which persist until program isn't continued.
class FrameId : public CustomScalarType<FrameId>
{
  public:

    using ScalarType = int;

    const static int32_t MaxFrameId = INT32_MAX;

    FrameId() : m_id(-1)
    {
    }
    FrameId(ThreadId, FrameLevel);
    FrameId(int);

    explicit operator ScalarType() const noexcept
    {
        return assert(*this), m_id;
    }
    explicit operator bool() const
    {
        return m_id != -1;
    }

    ThreadId getThread() const noexcept;
    FrameLevel getLevel() const noexcept;

    static void invalidate();

  private:

    ScalarType m_id;
};

struct Thread
{
    ThreadId id;
    std::string name;
    bool running;

    Thread(ThreadId id_, const std::string &name_, bool running_)
        : id(id_),
          name(name_),
          running(running_)
    {
    }
};

struct Source
{
    std::string name;
    std::string path;

    Source(const std::string &path = {});
    bool IsNull() const
    {
        return name.empty() && path.empty();
    }
};

struct ClrAddr
{
    uint32_t ilOffset;
    uint32_t nativeOffset;
    uint32_t methodToken;

    ClrAddr()
        : ilOffset(0),
          nativeOffset(0),
          methodToken(0)
    {
    }
    bool IsNull() const
    {
        return methodToken == 0;
    }
};

struct StackFrame
{
  public:

    FrameId id; // should be assigned only once, before calls to GetLevel or GetThreadId.
    std::string methodName;
    Source source;
    int line;
    int column;
    int endLine;
    int endColumn;
    std::string moduleId;

    StackFrame()
        : id(),
          line(0),
          column(0),
          endLine(0),
          endColumn(0),
          thread(ThreadId{}, true),
          level(FrameLevel{}, true)
    {
    }

    StackFrame(ThreadId threadId, FrameLevel level_, const std::string &methodName_)
        : id(FrameId(threadId, level_)),
          methodName(methodName_),
          line(0),
          column(0),
          endLine(0),
          endColumn(0),
          thread(threadId, true),
          level(level_, true)
    {
    }

    StackFrame(FrameId id)
        : id(id),
          line(0),
          column(0),
          endLine(0),
          endColumn(0),
          thread(ThreadId{}, false),
          level(FrameLevel{}, false)
    {
    }

    FrameLevel GetLevel() const
    {
        return std::get<1>(level) ? std::get<0>(level) : std::get<0>(level) = id.getLevel();
    }

    ThreadId GetThreadId() const
    {
        return std::get<1>(thread) ? std::get<0>(thread) : std::get<0>(thread) = id.getThread();
    }

  private:

    template <typename T> using Optional = std::tuple<T, bool>;

    mutable Optional<ThreadId> thread;
    mutable Optional<FrameLevel> level;
};

struct Breakpoint
{
    uint32_t id;
    bool verified;
    std::string message;
    Source source;
    int line;
    int endLine;

    std::string condition;
    std::string module;
    std::string funcname;
    std::string params;

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

struct Module
{
    std::string id;
    std::string name;
    std::string path;
    // bool isOptimized; // TODO: support both fields for DAP
    // bool isUserCode;
    SymbolStatus symbolStatus;

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

struct StoppedEvent
{
    StoppedEventReason reason;
    ThreadId threadId;
    std::string text;
    bool allThreadsStopped;

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

struct OutputEvent
{
    OutputCategory category;
    std::string output;
    StackFrame frame;

    OutputEvent(OutputCategory category_, const std::string &output_)
        : category(category_),
          output(output_),
          frame()
    {
    }
};

enum class ModuleEventReason
{
    New,
    Changed,
    Removed
};

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

struct Scope
{
    std::string name;
    uint32_t variablesReference;
    int namedVariables;
    int indexedVariables;
    bool expensive;

    Scope()
        : variablesReference(0),
          namedVariables(0),
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

// https://learn.microsoft.com/en-us/visualstudio/extensibility/debugger/reference/evalflags?view=visualstudio&tabs=cpp
enum enum_EVALFLAGS : uint32_t
{
    EVAL_RETURNVALUE = 0x0002,
    EVAL_NOSIDEEFFECTS = 0x0004,
    EVAL_ALLOWBPS = 0x0008,
    EVAL_ALLOWERRORREPORT = 0x0010,
    EVAL_FUNCTION_AS_ADDRESS = 0x0040,
    EVAL_NOFUNCEVAL = 0x0080,
    EVAL_NOEVENTS = 0x1000
};

constexpr uint32_t defaultEvalFlags = 0;

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
    uint32_t evalFlags;
    bool editable;

    Variable(uint32_t flags = defaultEvalFlags)
        : variablesReference(0),
          namedVariables(0),
          indexedVariables(0),
          evalFlags(flags),
          editable(false)
    {
    }
};

enum class VariablesFilter
{
    Named,
    Indexed,
    Both
};

struct LineBreakpoint
{
    std::string module;
    int line;
    std::string condition;

    LineBreakpoint(const std::string &module, int linenum, const std::string &cond = std::string())
        : module(module),
          line(linenum),
          condition(cond)
    {
    }
};

struct FuncBreakpoint
{
    std::string module;
    std::string func;
    std::string params;
    std::string condition;

    FuncBreakpoint(const std::string &module, const std::string &func, const std::string &params,
                   const std::string &cond = std::string())
        : module(module),
          func(func),
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

// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ExceptionInfo
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

enum class AsyncResult
{
    Canceled, // function canceled due to debugger interruption
    Error,    // IO error
    Eof       // EOF reached
};

enum class StepType
{
    STEP_IN = 0,
    STEP_OVER,
    STEP_OUT
};

struct SequencePoint
{
    int32_t startLine;
    int32_t startColumn;
    int32_t endLine;
    int32_t endColumn;
    int32_t offset;
    std::string document;
};

enum class ValueKind
{
    Scope,
    Class,
    Variable
};

} // namespace dncdbg
