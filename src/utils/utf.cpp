// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/utf.h"

#ifdef _WIN32
#include <stringapiset.h>
#else
#include <iconv.h>
#include <vector>
#endif

namespace dncdbg
{

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
static std::u16string utf8_to_utf16(const std::string &utf8_str)
{
    iconv_t conv = iconv_open("UTF-16LE", "UTF-8"); // Open converter from UTF-8 to UTF-16LE
    if (conv == reinterpret_cast<iconv_t>(-1)) // NOLINT(performance-no-int-to-ptr)
        return std::u16string();

    size_t in_bytes = utf8_str.size();
    char *in_buf = const_cast<char *>(utf8_str.c_str());
    size_t out_bytes = in_bytes * 2 + 2; // worst case UTF-16 is twice the size of UTF-8 in bytes
    std::vector<char> out_buf(out_bytes);
    char *out_ptr = out_buf.data();
    size_t original_out_bytes = out_bytes;

    if (iconv(conv, &in_buf, &in_bytes, &out_ptr, &out_bytes) == (size_t)-1)
    {
        iconv_close(conv);
        return std::u16string();
    }

    iconv_close(conv);

    size_t u16_len = (original_out_bytes - out_bytes) / sizeof(char16_t);
    return std::u16string(reinterpret_cast<char16_t *>(out_buf.data()), u16_len);
}

static std::string utf16_to_utf8(const std::u16string &utf16_str)
{
    iconv_t conv = iconv_open("UTF-8", "UTF-16LE"); // Open converter from UTF-16LE to UTF-8
    if (conv == reinterpret_cast<iconv_t>(-1)) // NOLINT(performance-no-int-to-ptr)
        return std::string();

    size_t in_bytes = utf16_str.size() * sizeof(char16_t);
    char *in_buf = reinterpret_cast<char *>(const_cast<char16_t *>(utf16_str.c_str()));
    size_t out_bytes = in_bytes * 2 + 2; // worst case UTF-8 is twice the size of UTF-16 in bytes
    std::vector<char> out_buf(out_bytes);
    char *out_ptr = out_buf.data();
    size_t original_out_bytes = out_bytes;

    if (iconv(conv, &in_buf, &in_bytes, &out_ptr, &out_bytes) == (size_t)-1)
    {
        iconv_close(conv);
        return std::string();
    }

    iconv_close(conv);

    size_t u8_len = (original_out_bytes - out_bytes) / sizeof(char);
    return std::string(reinterpret_cast<char *>(out_buf.data()), u8_len);
}
#endif // #if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

std::string to_utf8(const WCHAR *wstr_)
{
#ifdef _WIN32
    WSTRING wstr(wstr_);
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr);
    return str;
#else
    return utf16_to_utf8(wstr_);
#endif
}

WSTRING to_utf16(const std::string &utf8)
{
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), nullptr, 0);
    WSTRING wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), &wstr[0], count);
    return wstr;
#else
    return utf8_to_utf16(utf8);
#endif
}

} // namespace dncdbg
