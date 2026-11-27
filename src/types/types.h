// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef TYPES_TYPES_H
#define TYPES_TYPES_H

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <wtypes.h>
#endif

#include <cassert>
#include <climits>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace dncdbg
{

// This is a helper class that simplifies the creation of custom scalar types
// (ones that provide strong typing and disallow mixing with any other scalar types).
// Basically, these types support equality comparison operators and operator<
// (to allow using such types with STL containers).

template <typename T> struct CustomScalarType
{
    friend bool operator==(T a, T b) noexcept
    {
        return static_cast<typename T::ScalarType>(a) == static_cast<typename T::ScalarType>(b);
    }
    template <typename U> friend bool operator==(T a, U b) noexcept
    {
        return static_cast<typename T::ScalarType>(a) == b;
    }
    template <typename U> friend bool operator==(U a, T b) noexcept
    {
        return a == static_cast<typename T::ScalarType>(b);
    }
    friend bool operator!=(T a, T b) noexcept
    {
        return !(a == b);
    }
    template <typename U> friend bool operator!=(T a, U b) noexcept
    {
        return !(a == b);
    }
    template <typename U> friend bool operator!=(U a, T b) noexcept
    {
        return !(a == b);
    }

    bool operator<(const T &other) const noexcept
    {
        return static_cast<typename T::ScalarType>(static_cast<const T &>(*this)) <
               static_cast<typename T::ScalarType>(other);
    }

  private:

    CustomScalarType() = default;
    friend T;
};

// Data type dedicated to carry a thread ID.
class ThreadId : public CustomScalarType<ThreadId>
{
  public:

    using ScalarType = int;

    // This is for cases when ThreadId isn't initialized or is unknown.
    static const ThreadId Invalid;

    // This should be used as the Any/All threads marker in the protocol (for Emit...Event).
    static const ThreadId AllThreads;

    ThreadId() noexcept
        : ThreadId(Invalid)
    {
    }

    explicit ThreadId(int threadId) noexcept
        : m_id(threadId)
    {
        assert(threadId != InvalidValue && threadId != AllThreadsValue);
    }

    explicit ThreadId(DWORD threadId) noexcept
        : ThreadId(static_cast<int>(threadId))
    {
    }

    explicit operator bool() const noexcept
    {
        return m_id != InvalidValue;
    }

    explicit operator ScalarType() const noexcept
    {
        return assert(*this), m_id;
    }

  private:

    enum SpecialValues // NOLINT(performance-enum-size,cppcoreguidelines-use-enum-class)
    {
        InvalidValue = 0,
        AllThreadsValue = -1
    };

    int m_id;

    constexpr explicit ThreadId(SpecialValues val) noexcept
        : m_id(val)
    {
    }
};

// Data type dedicated to carry the stack frame depth (level).
class FrameLevel : public CustomScalarType<FrameLevel>
{
  public:

    using ScalarType = int;

    static constexpr int MaxFrameLevel = SHRT_MAX;

    FrameLevel() noexcept : m_level(-1)
    {
    }

    explicit FrameLevel(unsigned n) noexcept
        : m_level{(assert(static_cast<int>(n) <= MaxFrameLevel), static_cast<int>(n))}
    {
    }
    explicit FrameLevel(int n) noexcept
        : FrameLevel(static_cast<unsigned>(n))
    {
    }

    explicit operator ScalarType() const noexcept
    {
        return assert(*this), m_level;
    }
    explicit operator bool() const noexcept
    {
        return m_level != -1;
    }

  private:

    int m_level;
};

// Unique stack frame identifier, which persists until the program is continued.
class FrameId : public CustomScalarType<FrameId>
{
  public:

    using ScalarType = int;

    static constexpr int32_t MaxFrameId = INT32_MAX;

    FrameId() noexcept : m_id(-1)
    {
    }
    FrameId(ThreadId thread, FrameLevel level);
    explicit FrameId(int n) noexcept;

    explicit operator ScalarType() const noexcept
    {
        return assert(*this), m_id;
    }
    explicit operator bool() const noexcept
    {
        return m_id != -1;
    }

    [[nodiscard]] ThreadId GetThread() const noexcept;
    [[nodiscard]] FrameLevel GetLevel() const noexcept;

    static void Cleanup();

  private:

    ScalarType m_id;
};

enum class FormatSpecifier : uint16_t
{
    None                       = 0,
    ForceEvaluation            = 1U,
    DecimalInteger             = 1U << 1U,
    HexadecimalInteger         = 1U << 2U,
    Dynamic                    = 1U << 3U,
    EvaluatesWithNoSideEffects = 1U << 4U,
    StringWithNoQuotes         = 1U << 5U,
    DisplaysHiddenMembers      = 1U << 6U,
    DisplaysInRawMode          = 1U << 7U,
    Results                    = 1U << 8U,
    WalkContainerMembers       = 1U << 9U
};

constexpr FormatSpecifier operator | (FormatSpecifier lhs, FormatSpecifier rhs) noexcept
{
    using T = std::underlying_type_t<FormatSpecifier>;
    return static_cast<FormatSpecifier>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

constexpr FormatSpecifier operator & (FormatSpecifier lhs, FormatSpecifier rhs) noexcept
{
    using T = std::underlying_type_t<FormatSpecifier>;
    return static_cast<FormatSpecifier>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

enum class StepType : uint8_t
{
    STEP_IN = 0,
    STEP_OVER,
    STEP_OUT
};

enum class ValueKind : uint8_t
{
    Scope,
    Static,
    Variable
};

enum class HitConditionOp : uint8_t
{
    None,           // hit condition not set
    Less,           // <N
    LessOrEqual,    // <=N
    Equal,          // ==N
    Greater,        // >N
    GreaterOrEqual, // N or >=N
    EveryN          // %N
};

struct HitCondition
{
    HitConditionOp op{HitConditionOp::None};
    uint32_t n{0};

    constexpr bool operator==(const HitCondition &other) const noexcept
    {
        return op == other.op && n == other.n;
    }

    constexpr bool operator!=(const HitCondition &other) const noexcept
    {
        return !(*this == other);
    }
};

} // namespace dncdbg

#endif // TYPES_TYPES_H
