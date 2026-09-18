// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/downloader.h"
#include "utils/utf.h"
#include <windows.h>
#include <wininet.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dncdbg
{

namespace
{

constexpr DWORD requestTimeoutMs = 60 * 1000;
constexpr DWORD httpStatusOk = 200;
constexpr DWORD httpStatusMultipleChoices = 300;

// Closes WinINet handles when they go out of scope.
struct InternetHandleDeleter
{
    void operator()(HINTERNET handle) const noexcept
    {
        if (handle != nullptr)
        {
            InternetCloseHandle(handle);
        }
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleDeleter>;

// Formats `operation` together with the system message for `error`. When
// FormatMessageW() has no message, the numeric error code is used instead.
std::string Win32ErrorMessage(const char *operation, DWORD error)
{
    LPWSTR messageBuffer = nullptr;
    const DWORD messageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    std::string message(operation);
    message += " failed";
    if (messageLength != 0 && messageBuffer != nullptr)
    {
        message += ": ";
        message += to_utf8(messageBuffer);
        // FormatMessageW() appends a trailing CRLF.
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        {
            message.pop_back();
        }
    }
    else
    {
        message += " (error " + std::to_string(error) + ")";
    }

    if (messageBuffer != nullptr)
    {
        LocalFree(messageBuffer);
    }
    return message;
}

} // namespace

bool DownloadSource(const std::string &urlStr, std::string &output)
{
    output.clear();
    if (urlStr.empty())
    {
        output = "URL must not be empty";
        return false;
    }

    const WSTRING urlWStr = to_utf16(urlStr);
    if (urlWStr.empty())
    {
        output = "URL is not valid UTF-8";
        return false;
    }

    // InternetOpenUrlW also accepts schemes such as ftp://, but the status
    // code query below is HTTP-specific, so require HTTP or HTTPS explicitly.
    const bool isHttp = urlWStr.size() >= 7 &&
        CompareStringOrdinal(urlWStr.data(), 7, L"http://", 7, TRUE) == CSTR_EQUAL;
    const bool isHttps = urlWStr.size() >= 8 &&
        CompareStringOrdinal(urlWStr.data(), 8, L"https://", 8, TRUE) == CSTR_EQUAL;
    const size_t authorityStart = isHttp ? 7 : (isHttps ? 8 : 0);
    const size_t authorityEnd = urlWStr.find_first_of(L"/?#", authorityStart);
    const size_t authorityLength =
        (authorityEnd == WSTRING::npos ? urlWStr.size() : authorityEnd) - authorityStart;
    if (authorityStart == 0 || authorityLength == 0)
    {
        output = "URL must be an absolute HTTP or HTTPS URL";
        return false;
    }

    InternetHandle internet(InternetOpenW(L"dncdbg", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
    if (!internet)
    {
        output = Win32ErrorMessage("InternetOpen", GetLastError());
        return false;
    }

    DWORD timeoutMs = requestTimeoutMs;
    InternetSetOptionW(internet.get(), INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(internet.get(), INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(internet.get(), INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    constexpr DWORD requestFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    InternetHandle url(InternetOpenUrlW(internet.get(), urlWStr.c_str(), nullptr, 0, requestFlags, 0));
    if (!url)
    {
        output = Win32ErrorMessage("InternetOpenUrl", GetLastError());
        return false;
    }

    // InternetOpenUrlW follows redirects automatically, so the status code
    // corresponds to the final response.
    DWORD statusCode = 0;
    DWORD statusCodeLength = sizeof(statusCode);
    if (!HttpQueryInfoW(url.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                        &statusCode, &statusCodeLength, nullptr))
    {
        output = Win32ErrorMessage("HttpQueryInfo", GetLastError());
        return false;
    }
    if (statusCode < httpStatusOk || statusCode >= httpStatusMultipleChoices)
    {
        output = "HTTP request failed with status code " + std::to_string(statusCode);
        return false;
    }

    std::string responseData;
    std::vector<char> buffer(16 * 1024);
    for (;;)
    {
        DWORD bytesRead = 0;
        if (!InternetReadFile(url.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead))
        {
            output = Win32ErrorMessage("InternetReadFile", GetLastError());
            return false;
        }
        if (bytesRead == 0)
        {
            break;
        }
        responseData.append(buffer.data(), bytesRead);
    }

    output = std::move(responseData);
    return true;
}

} // namespace dncdbg

#endif // _WIN32
