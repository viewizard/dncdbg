// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "types/types.h"
#include <algorithm>
#include <cstddef> // ptrdiff_t
#include <mutex>
#include <tuple>
#include <vector>

// Important! All "types" code must not depends from other debugger's code.

namespace dncdbg
{

// ThreadId == 0 is invalid for Win32 API and PAL library.
const ThreadId ThreadId::Invalid{InvalidValue};

const ThreadId ThreadId::AllThreads{AllThreadsValue};

// This namespace holds the list of frames accessible by index value;
// this list expires every time the program continues execution.
namespace KnownFrames
{

namespace
{

// This class implements a container, which holds elements of type `T', where
// elements are addressed by an integral value of type `Key'. The `Key' can
// take values from 0 to `Max' inclusive. The value of `Key' is not provided
// initially, but is assigned automatically when a new value of type `T' is
// added to the container. Later, this value can be accessed by using the
// assigned key value. This container type doesn't allow duplicate values of
// type `T': each unique `T' value corresponds to exactly one unique `Key'
// value. This container tries to avoid reusing a `Key' value after a call
// to `clear()' (it uses a cyclically incremented number, which wraps around
// after reaching the `Max' value).
template <typename Key, typename T, Key Max = std::numeric_limits<Key>::max(),
          class = typename std::enable_if_t<std::is_unsigned_v<Key>>>
class IndexedStorage
{
  public:

    // Data type used as the key.
    using KeyType = Key;

    // Data type stored in the container.
    using MappedType = T;

    // Data type, which combines a key with an element stored in the container.
    using ValueType = std::pair<KeyType, MappedType>;

    // These definitions are needed for standard library algorithms.
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = ValueType &;
    using pointer = ValueType *;

    // This class implements at least an input iterator,
    // which allows iterating over all elements stored in the container.
    using iterator = typename std::vector<ValueType>::const_iterator;

    // Return an iterator pointing to the first element.
    [[nodiscard]] iterator begin() const
    {
        return m_data.cbegin();
    }

    // Return an iterator pointing beyond the last element.
    [[nodiscard]] iterator end() const
    {
        return m_data.cend();
    }

    // Constructor which creates a new empty container.
    IndexedStorage()
        : m_base(0)
    {
    }

    // Return the number of elements currently stored in the container.
    [[nodiscard]] size_type size() const
    {
        return m_data.size();
    }

    // Erase all contents.
    void clear()
    {
        m_base += KeyType(m_data.size());
        m_data.clear();
    }

    // This function creates a new element from the supplied arguments and
    // returns a pair containing an iterator pointing to the newly created
    // element and a boolean value which is false when the element is already
    // present in the container, or true if a new element was created.
    template <typename... Args> std::pair<iterator, bool> emplace(Args &&...args)
    {
        MappedType data{std::forward<Args &&>(args)...};
        return insert(std::move(data));
    }

    // This function inserts a new element into the container and returns a
    // pair consisting of an iterator pointing to the element which was
    // inserted into the container; the second element of the pair is a
    // boolean value which is set to false if the element was already present
    // in the container, or set to true if a new element was added.
    std::pair<iterator, bool> insert(const ValueType &val)
    {
        const auto it = DoInsert(val.second);
        if (it != m_data.cend())
        {
            return {it, false};
        }
        m_data.push_back(ValueType(NextId(), val.second));
        return {--m_data.cend(), true};
    }

    // This function does the same as the previous one, but avoids copying `val'.
    std::pair<iterator, bool> insert(MappedType &&val)
    {
        const auto it = DoInsert(val);
        if (it != m_data.cend())
        {
            return {it, false};
        }
        m_data.push_back(ValueType(NextId(), std::move(val)));
        return {--m_data.cend(), true};
    }

    // This function finds the data element which corresponds to the supplied `key'
    // and returns an iterator pointing to it, or returns an iterator pointing
    // to the `end()` value if no element corresponds to the supplied `key'.
    [[nodiscard]] iterator find(KeyType key) const noexcept
    {
        try
        {
            if (m_base > key)
            {
                return end();
            }

            const KeyType index = key - m_base;
            if (index >= m_data.size() || m_data.at(index).first != key)
            {
                return end();
            }

            return begin() + index;
        }
        catch (...)
        {
            return end();
        }
    }

    // This function checks if an element with the corresponding `key' value is present in the container.
    [[nodiscard]] bool contains(KeyType key) const
    {
        return find(key) != end();
    }

  private:

    KeyType m_base;
    std::vector<ValueType> m_data;

    iterator DoInsert(const MappedType &val)
    {
        return std::find_if(m_data.cbegin(), m_data.cend(),
                            [&](const ValueType &other) { return other.second == val; });
    }

    [[nodiscard]] KeyType NextId() const
    {
        const auto dataSize = static_cast<KeyType>(m_data.size());

        if (m_base > Max - dataSize)
        {
            // Calculate (m_base + dataSize - Max) without result overflow or underflow for the unsigned "Key" type.
            return Max - (Max - m_base) - (Max - dataSize);
        }

        return m_base + dataSize;
    }
};

struct State
{
    std::mutex mutex;
    IndexedStorage<unsigned, std::tuple<ThreadId, FrameLevel>> list;
};

State &GetState()
{
    static State state;
    return state;
}

// This function creates a new element for the supplied thread and frame level,
// and returns the key value assigned to it.
unsigned Emplace(ThreadId thread, FrameLevel level)
{
    State &state = GetState();
    const std::scoped_lock lock(state.mutex);
    return state.list.emplace(thread, level).first->first;
}

// This function returns the thread id for the supplied `key',
// or a default-constructed value if no element corresponds to the `key'.
ThreadId GetThread(unsigned key)
{
    State &state = GetState();
    const std::scoped_lock lock(state.mutex);
    const auto it = state.list.find(key);
    if (it == state.list.end())
    {
        return {};
    }
    return std::get<0>(it->second);
}

// This function returns the frame level for the supplied `key',
// or a default-constructed value if no element corresponds to the `key'.
FrameLevel GetLevel(unsigned key)
{
    State &state = GetState();
    const std::scoped_lock lock(state.mutex);
    const auto it = state.list.find(key);
    if (it == state.list.end())
    {
        return {};
    }
    return std::get<1>(it->second);
}

// Erase all contents.
void Cleanup()
{
    State &state = GetState();
    const std::scoped_lock lock(state.mutex);
    state.list.clear();
}

} // unnamed namespace

} // namespace KnownFrames

FrameId::FrameId(ThreadId thread, FrameLevel level)
    : m_id(static_cast<ScalarType>(KnownFrames::Emplace(thread, level)))
{
}

FrameId::FrameId(int n) : m_id(n)
{
}

ThreadId FrameId::GetThread() const noexcept
{
    if (m_id != -1)
    {
        return KnownFrames::GetThread(static_cast<unsigned>(m_id));
    }
    return {};
}

FrameLevel FrameId::GetLevel() const noexcept
{
    if (m_id != -1)
    {
        return KnownFrames::GetLevel(static_cast<unsigned>(m_id));
    }
    return {};
}

void FrameId::Cleanup()
{
    KnownFrames::Cleanup();
}

} // namespace dncdbg
