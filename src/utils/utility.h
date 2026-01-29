// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once
#include <stddef.h>

namespace dncdbg
{

namespace Utility
{

// This function is similar to `std::size()` from C++17 and allows to determine
// the object size as number of elements, which might be stored within object
// (opposed to sizeof(), which returns object sizes in bytes). Typically these
// functions applicable to arrays and to classes like std::array.
template <typename T> constexpr auto Size(const T &v) -> decltype(v.size())
{
    return v.size();
}
template <class T, size_t N> constexpr size_t Size(const T (&)[N]) noexcept
{
    return N;
}

// This template is similar to `offsetof` macros in plain C. It allows
// to get offset of specified member in some particular class.
template <typename Owner, typename Member> static inline constexpr size_t offset_of(const Member Owner::*mem)
{
    return reinterpret_cast<size_t>(&(reinterpret_cast<Owner *>(0)->*mem));
}

// This template is similar to well known `container_of` macros. It allows
// to get pointer to owner class from pointer to member.
template <typename Owner, typename Member>
static inline constexpr Owner *container_of(Member *ptr, const Member Owner::*mem)
{
    return reinterpret_cast<Owner *>(reinterpret_cast<size_t>(ptr) - offset_of(mem));
}

} // namespace Utility
} // namespace dncdbg
