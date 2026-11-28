// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/utf.h"

#include <cstddef>

#ifdef _WIN32
#include <climits>
#include <stringapiset.h>
#else
#include <cerrno>
#include <cstdint>
#include <iconv.h>
#include <limits>
#endif

namespace dncdbg
{

namespace
{

#ifdef FEATURE_PAL

// Result of IconvDescriptor::Convert().
enum class ConvResult : uint8_t
{
    Ok,      // All input was converted.
    NoSpace, // The output buffer is too small; the conversion can be resumed.
    Error    // The input is invalid or incomplete; the conversion is aborted.
};

// iconv_open() resolves and caches the conversion modules, so it is much more expensive than
// the conversion itself. The descriptor is created once per thread and reused afterwards.
// The conversion state is stored in the descriptor, so a descriptor cannot be shared
// between threads.
class IconvDescriptor
{
public:
    IconvDescriptor(const char *toCode, const char *fromCode) : descriptor(iconv_open(toCode, fromCode)) {}

    ~IconvDescriptor()
    {
        if (IsOpen())
        {
            iconv_close(descriptor);
        }
    }

    // Non-copyable, non-movable.
    IconvDescriptor(const IconvDescriptor &) = delete;
    IconvDescriptor(IconvDescriptor &&) = delete;
    IconvDescriptor &operator=(const IconvDescriptor &) = delete;
    IconvDescriptor &operator=(IconvDescriptor &&) = delete;

    [[nodiscard]] bool IsOpen() const
    {
        // iconv_open() returns (iconv_t)-1 on error; the check compares the integer value.
        return reinterpret_cast<std::intptr_t>(descriptor) != -1;
    }

    // Converts input from `inBuf` (`inBytesLeft` bytes) into `outBuf` (`outBytesLeft` bytes).
    // On return, the pointers and counters point to the first unconsumed input and the first
    // free output byte, so the conversion can be resumed after ConvResult::NoSpace.
    [[nodiscard]] ConvResult Convert(const char *&inBuf, std::size_t &inBytesLeft, char *&outBuf,
        std::size_t &outBytesLeft)
    {
        char *inPtr = const_cast<char *>(inBuf); // NOLINT(cppcoreguidelines-pro-type-const-cast)

        const std::size_t rc = iconv(descriptor, &inPtr, &inBytesLeft, &outBuf, &outBytesLeft);
        inBuf = inPtr;

        if (rc != static_cast<std::size_t>(-1))
        {
            return ConvResult::Ok;
        }
        if (errno == E2BIG)
        {
            return ConvResult::NoSpace;
        }

        // Drop the state of the failed conversion, so the next call starts from the initial state.
        iconv(descriptor, nullptr, nullptr, nullptr, nullptr);
        return ConvResult::Error;
    }

private:
    iconv_t descriptor;
};

std::u16string utf8_to_utf16(const char *utf8_str, std::size_t utf8_len)
{
#if BIGENDIAN
    static constexpr const char *to_code = "UTF-16BE";
#else
    static constexpr const char *to_code = "UTF-16LE";
#endif
    // Convert from UTF-8 to UTF-16; the descriptor is opened once per thread.
    static thread_local IconvDescriptor conv(to_code, "UTF-8");

    if (utf8_len == 0 || !conv.IsOpen())
    {
        return {};
    }

    // The check rejects sizes that would make the size arithmetic below overflow.
    if (utf8_len > (std::numeric_limits<std::size_t>::max)() / sizeof(char16_t))
    {
        return {};
    }

    // UTF-8 never needs more UTF-16 code units than input bytes: a 1-3 byte sequence gives one
    // code unit, and a 4-byte sequence gives two code units. So an upper bound is allocated
    // once, and the conversion never needs more space than that.
    std::u16string out(utf8_len, u'\0');
    char *outBuf = reinterpret_cast<char *>(out.data());
    std::size_t outBytesLeft = utf8_len * sizeof(char16_t);
    const char *inBuf = utf8_str;
    std::size_t inBytesLeft = utf8_len;

    if (conv.Convert(inBuf, inBytesLeft, outBuf, outBytesLeft) != ConvResult::Ok)
    {
        return {};
    }

    // Shrink in place: the buffer is not reallocated, no copy is made.
    out.resize((utf8_len * sizeof(char16_t) - outBytesLeft) / sizeof(char16_t));
    return out;
}

std::string utf16_to_utf8(const char16_t *utf16_str, std::size_t utf16_len)
{
#if BIGENDIAN
    static constexpr const char *from_code = "UTF-16BE";
#else
    static constexpr const char *from_code = "UTF-16LE";
#endif
    // Convert from UTF-16 to UTF-8; the descriptor is opened once per thread.
    static thread_local IconvDescriptor conv("UTF-8", from_code);

    if (utf16_len == 0 || !conv.IsOpen())
    {
        return {};
    }

    // A UTF-16 code unit gives at most 3 bytes of UTF-8 (a surrogate pair gives 4 bytes for 2 units).
    // The check rejects sizes that would make the size arithmetic below overflow.
    if (utf16_len > (std::numeric_limits<std::size_t>::max)() / 3)
    {
        return {};
    }

    // Most text is ASCII, where one UTF-16 code unit gives exactly one byte of UTF-8. The buffer
    // starts with that size and grows only when the conversion reports NoSpace, so typical data
    // does not reserve memory for the worst case.
    std::string out(utf16_len, '\0');
    const char *inBuf = reinterpret_cast<const char *>(utf16_str);
    std::size_t inBytesLeft = utf16_len * sizeof(char16_t);
    std::size_t outUsed = 0;

    for (;;)
    {
        char *outBuf = out.data() + outUsed;
        std::size_t outBytesLeft = out.size() - outUsed;

        const ConvResult result = conv.Convert(inBuf, inBytesLeft, outBuf, outBytesLeft);
        outUsed = static_cast<std::size_t>(outBuf - out.data());

        if (result == ConvResult::Ok)
        {
            break;
        }
        if (result == ConvResult::Error)
        {
            return {};
        }

        // Reserve the worst case for the remaining input, which is at most 3 bytes per code unit.
        // The bytes already written are kept, and the conversion resumes where it stopped.
        // Rounding up guarantees room for at least one code unit, so the loop always makes progress.
        out.resize(outUsed + ((inBytesLeft + sizeof(char16_t) - 1) / sizeof(char16_t)) * 3);
    }

    // Shrink in place: the buffer is not reallocated, no copy is made.
    out.resize(outUsed);
    return out;
}

#endif // FEATURE_PAL

} // unnamed namespace

std::string to_utf8(const WCHAR *wstr_)
{
    if (!wstr_)
    {
        return {};
    }

#ifdef _WIN32
    // Measure the input in place; copying it into a temporary WSTRING is unnecessary.
    const std::size_t wstr_len = std::char_traits<WCHAR>::length(wstr_);
    if (wstr_len == 0 || wstr_len > static_cast<std::size_t>(INT_MAX))
    {
        return {};
    }
    const int wstr_units = static_cast<int>(wstr_len);

    // Ask for the required buffer size first, so the result is allocated exactly once.
    const int count = WideCharToMultiByte(CP_UTF8, 0, wstr_, wstr_units, nullptr, 0, nullptr, nullptr);
    if (count <= 0)
    {
        return {};
    }

    std::string str(static_cast<std::size_t>(count), '\0');
    // The length is passed explicitly instead of -1, so the terminating null character is
    // not included in the result.
    if (WideCharToMultiByte(CP_UTF8, 0, wstr_, wstr_units, str.data(), count, nullptr, nullptr) != count)
    {
        return {};
    }
    return str;
#else
    // WCHAR is char16_t here, so the length is measured in place without a temporary WSTRING.
    return utf16_to_utf8(wstr_, std::char_traits<WCHAR>::length(wstr_));
#endif
}

WSTRING to_utf16(std::string_view utf8)
{
#ifdef _WIN32
    if (utf8.empty() || utf8.size() > static_cast<std::size_t>(INT_MAX))
    {
        return {};
    }
    const int utf8_bytes = static_cast<int>(utf8.size());

    // Ask for the required buffer size first, so the result is allocated exactly once.
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8_bytes, nullptr, 0);
    if (count <= 0)
    {
        return {};
    }

    WSTRING wstr(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8_bytes, wstr.data(), count) != count)
    {
        return {};
    }
    return wstr;
#else
    // The UTF-8 data is passed as a pointer and a length, so no temporary copy is created.
    return utf8_to_utf16(utf8.data(), utf8.size());
#endif
}

} // namespace dncdbg
