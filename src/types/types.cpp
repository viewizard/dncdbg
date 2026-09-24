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

// This class implements container, which hold elements of type `T', where
// elements addressed by integral value of type `Key'. The `Key' can take
// values from 0 to `Max' inclusively. The value of `Key' is not provided
// initially, but automatically assigned when new value of type `T' is
// added to container. Later, this value might be accessed by using assigned
// key value. This container type doesn't allows duplicate values of type `T':
// for each unique `T' value corresponds only one unique `Key' value.
// This container tries avoid using of repeated `Key' value after call
// to `clear()' (it uses cyclically increased number, which wraps around
// after reaching `Max' value).
template <typename Key, typename T, Key Max = std::numeric_limits<Key>::max(),
          class = typename std::enable_if_t<std::is_unsigned_v<Key>>>
class IndexedStorage
{
  public:

    // Data type used as the key.
    using key_type = Key;

    // Data type stored in container.
    using mapped_type = T;

    // Data type, which combines key with element stored in container.
    using value_type = std::pair<key_type, mapped_type>;

    // These definitions needed for standard library algorithms.
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = value_type &;
    using pointer = value_type *;

    // This class implements at least input-iterator
    // which allows to iterate over all elements stored in container.
    using iterator = typename std::vector<value_type>::const_iterator;

    // Return iterator pointing on first element.
    [[nodiscard]] iterator begin() const
    {
        return m_data.cbegin();
    }

    // Return iterator pointing beyond last element.
    [[nodiscard]] iterator end() const
    {
        return m_data.cend();
    }

    // Constructor which creates new empty container.
    IndexedStorage()
        : m_base(0)
    {
    }

    // Return number of elements currently stored in container.
    [[nodiscard]] size_type size() const
    {
        return m_data.size();
    }

    // Erase all contents.
    void clear()
    {
        m_base += key_type(m_data.size());
        m_data.clear();
    }

    // This function creates new element from supplied arguments and returns
    // pair containing iterator pointing to new created element and boolean
    // value which is false when element already present in container or true
    // if new element was created.
    template <typename... Args> std::pair<iterator, bool> emplace(Args &&...args)
    {
        mapped_type data{std::forward<Args &&>(args)...};
        return insert(std::move(data));
    }

    // This function inserts new element to container and returning pair
    // consisting of an iterator pointing to element which was inserted into
    // the container and second element of the pair is boolean value which is
    // set to false if element was already present in the container or set to
    // true, if new element was added.
    std::pair<iterator, bool> insert(const value_type &val)
    {
        const auto it = do_insert(val);
        if (it != m_data.cend())
        {
            return {it, false};
        }
        m_data.push_back(value_type(next_id(), val));
        return {--m_data.cend(), true};
    }

    // This function do the same as previous, but avoid copying of `val'.
    std::pair<iterator, bool> insert(mapped_type &&val)
    {
        const auto it = do_insert(val);
        if (it != m_data.cend())
        {
            return {it, false};
        }
        m_data.push_back(value_type(next_id(), std::move(val)));
        return {--m_data.cend(), true};
    }

    // This function finds the data element which corresponds to the supplied `key'
    // and returns an iterator pointing to it, or returns an iterator pointing
    // to `end()` value if no element corresponds to the supplied `key'.
    [[nodiscard]] iterator find(key_type key) const noexcept
    {
        try
        {
            if (m_base > key)
            {
                return end();
            }

            const key_type index = key - m_base;
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

    // This function checks if element with corresponding `key' value present in the container.
    [[nodiscard]] bool contains(key_type key) const
    {
        return find(key) != end();
    }

  private:

    key_type m_base;
    std::vector<value_type> m_data;

    iterator do_insert(const mapped_type &val)
    {
        return std::find_if(m_data.cbegin(), m_data.cend(),
                            [&](const value_type &other) { return other.second == val; });
    }

    [[nodiscard]] key_type next_id() const
    {
        const auto data_size = static_cast<key_type>(m_data.size());

        if (m_base > Max - data_size)
        {
            // calculate (m_base + data_size - Max) without result overflow and underflow for unsigned "Key" type
            return Max - (Max - m_base) - (Max - data_size);
        }

        return m_base + data_size;
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
