// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_INT_TO_STRING_H
#define UTILS_INT_TO_STRING_H

#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>

namespace dncdbg
{

// Appends the decimal representation of an integer to `out`. The buffer size is derived from the type:
// digits10 + 2 is always enough to hold the optional sign and every digit of the largest possible value of T.
// std::to_chars() writes into a stack buffer, so no temporary std::string is created.
template <typename T>
void AppendInteger(std::string &out, T value)
{
    static_assert(std::is_integral_v<T>, "AppendInteger() requires an integral type");
    constexpr std::size_t maxChars = static_cast<std::size_t>(std::numeric_limits<T>::digits10) + 2;
    std::array<char, maxChars> buffer{};
    const auto conv = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), conv.ptr);
}

// Returns the decimal representation of an integer. See AppendInteger().
template <typename T>
std::string IntegerToString(T value)
{
    std::string result;
    AppendInteger(result, value);
    return result;
}

} // namespace dncdbg

#endif // UTILS_INT_TO_STRING_H