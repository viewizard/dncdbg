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
#include <string>
#include <unordered_set>

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

    ThreadId(SpecialValues val) noexcept
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

    static constexpr int32_t MaxFrameId = INT32_MAX;

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

enum class VariablesFilter
{
    Named,
    Indexed,
    Both
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
