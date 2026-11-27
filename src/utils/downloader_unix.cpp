// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if defined(FEATURE_PAL) && !defined(__APPLE__)

#include "utils/downloader.h"
#include "config/config.h"
#include "utils/int_to_string.h"
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace dncdbg
{

namespace
{

constexpr long httpStatusOk = 200;
constexpr long httpStatusMultipleChoices = 300;

// The Content-Length header is controlled by the remote server, so it is only
// used as an allocation hint and is never allowed to exceed this limit. This
// keeps a broken or malicious server from forcing a huge allocation before any
// data arrives, while still covering the size of typical source files.
constexpr std::size_t maxPreallocatedResponseSize = 16U * 1024U * 1024U;

// Name of the response header that carries the body size, including the colon.
constexpr std::string_view contentLengthHeader = "content-length:";

// libcurl expresses its timeouts in whole seconds.
constexpr uint32_t millisecondsPerSecond = 1000;

// Restrict both direct requests and redirect targets to HTTP and HTTPS.
// libcurl 7.85.0 deprecated the numeric protocol options in favor of the
// string-based variants, so the variant matching the build-time headers is
// selected here.
#if LIBCURL_VERSION_NUM >= 0x075500 // libcurl 7.85.0
constexpr const char *allowedProtocolsStr = "http,https";
#else
constexpr long allowedProtocols = CURLPROTO_HTTP | CURLPROTO_HTTPS;
#endif

using CurlGlobalInit = CURLcode (*)(long);
using CurlEasyInit = CURL *(*)();
using CurlEasySetopt = CURLcode (*)(CURL *, CURLoption, ...);
using CurlEasyPerform = CURLcode (*)(CURL *);
using CurlEasyGetinfo = CURLcode (*)(CURL *, CURLINFO, ...);
using CurlEasyStrerror = const char *(*)(CURLcode);
using CurlEasyCleanup = void (*)(CURL *);
using CurlGlobalCleanup = void (*)();

struct CurlApi
{
    CurlGlobalInit globalInit = nullptr;
    CurlEasyInit easyInit = nullptr;
    CurlEasySetopt easySetopt = nullptr;
    CurlEasyPerform easyPerform = nullptr;
    CurlEasyGetinfo easyGetinfo = nullptr;
    CurlEasyStrerror easyStrerror = nullptr;
    CurlEasyCleanup easyCleanup = nullptr;
    CurlGlobalCleanup globalCleanup = nullptr;
};

// Closes the loaded library when it goes out of scope.
struct DynamicLibrary
{
    DynamicLibrary() = default;
    DynamicLibrary(DynamicLibrary &&) = delete;
    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(DynamicLibrary &&) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;

    ~DynamicLibrary()
    {
        if (handle != nullptr)
        {
            dlclose(handle);
        }
    }

    void *handle = nullptr;
};

// Holds the downloaded body. The same object is passed to both libcurl
// callbacks: the header callback reserves storage in `data`, and the write
// callback appends the received chunks to it.
struct ResponseBuffer
{
    std::string data;
};

// Case-insensitive comparison only covers ASCII letters, which is enough for
// URL schemes and HTTP header names. All other bytes are compared as-is.
constexpr char ToLowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

constexpr bool EqualsAsciiCaseInsensitive(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (ToLowerAscii(lhs.at(i)) != ToLowerAscii(rhs.at(i)))
        {
            return false;
        }
    }
    return true;
}

// Reserves storage for the body based on a Content-Length value. The value is
// capped because it comes from the remote server. The string never shrinks, so
// the largest reservation made so far determines the allocated size.
void ReserveResponse(ResponseBuffer &response, uint64_t declaredSize)
{
    if (declaredSize <= response.data.capacity())
    {
        return;
    }

    const std::size_t size = declaredSize < maxPreallocatedResponseSize
        ? static_cast<std::size_t>(declaredSize)
        : maxPreallocatedResponseSize;
    response.data.reserve(size);
}

// Header callback. Parses Content-Length so the body buffer can be allocated
// once instead of growing geometrically while data arrives. Returning a value
// other than `size * itemCount` aborts the transfer. The signature must match
// curl_write_callback.
// NOLINTNEXTLINE(misc-const-correctness,readability-non-const-parameter)
size_t HeaderCallback(char *buffer, size_t size, size_t itemCount, void *userData)
{
    if (itemCount != 0 && size > std::numeric_limits<size_t>::max() / itemCount)
    {
        return 0;
    }

    const size_t byteCount = size * itemCount;
    if (byteCount <= contentLengthHeader.size())
    {
        return byteCount;
    }

    const std::string_view line(buffer, byteCount);
    if (!EqualsAsciiCaseInsensitive(line.substr(0, contentLengthHeader.size()), contentLengthHeader))
    {
        return byteCount;
    }

    const char *const end = buffer + byteCount;
    const char *cursor = buffer + contentLengthHeader.size();
    while (cursor != end && (*cursor == ' ' || *cursor == '\t'))
    {
        ++cursor;
    }

    uint64_t declaredSize = 0;
    const auto result = std::from_chars(cursor, end, declaredSize);
    if (result.ec == std::errc{} && result.ptr != cursor)
    {
        ReserveResponse(*static_cast<ResponseBuffer *>(userData), declaredSize);
    }
    return byteCount;
}

// Appends the received chunk to the response buffer. Returning a value other
// than the chunk size aborts the transfer. The signature must match
// curl_write_callback.
// NOLINTNEXTLINE(misc-const-correctness,readability-non-const-parameter)
size_t WriteCallback(char *contents, size_t size, size_t itemCount, void *userData)
{
    if (itemCount != 0 && size > std::numeric_limits<size_t>::max() / itemCount)
    {
        return 0;
    }

    const size_t byteCount = size * itemCount;
    if (byteCount != 0)
    {
        static_cast<ResponseBuffer *>(userData)->data.append(contents, byteCount);
    }
    return byteCount;
}

// dlsym() returns nullptr both on error and for a symbol with a null value,
// so dlerror() is used to distinguish these cases.
void *LoadSymbol(void *handle, const char *name)
{
    dlerror();
    void *const symbol = dlsym(handle, name);
    return dlerror() == nullptr ? symbol : nullptr;
}

bool LoadCurlApi(void *handle, CurlApi &api)
{
    api.globalInit = reinterpret_cast<CurlGlobalInit>(LoadSymbol(handle, "curl_global_init"));
    api.easyInit = reinterpret_cast<CurlEasyInit>(LoadSymbol(handle, "curl_easy_init"));
    api.easySetopt = reinterpret_cast<CurlEasySetopt>(LoadSymbol(handle, "curl_easy_setopt"));
    api.easyPerform = reinterpret_cast<CurlEasyPerform>(LoadSymbol(handle, "curl_easy_perform"));
    api.easyGetinfo = reinterpret_cast<CurlEasyGetinfo>(LoadSymbol(handle, "curl_easy_getinfo"));
    api.easyStrerror = reinterpret_cast<CurlEasyStrerror>(LoadSymbol(handle, "curl_easy_strerror"));
    api.easyCleanup = reinterpret_cast<CurlEasyCleanup>(LoadSymbol(handle, "curl_easy_cleanup"));
    api.globalCleanup = reinterpret_cast<CurlGlobalCleanup>(LoadSymbol(handle, "curl_global_cleanup"));

    return api.globalInit != nullptr &&
        api.easyInit != nullptr &&
        api.easySetopt != nullptr &&
        api.easyPerform != nullptr &&
        api.easyGetinfo != nullptr &&
        api.easyStrerror != nullptr &&
        api.easyCleanup != nullptr &&
        api.globalCleanup != nullptr;
}

bool HasHttpScheme(std::string_view url)
{
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos)
    {
        return false;
    }

    const std::string_view scheme = url.substr(0, schemeEnd);
    return EqualsAsciiCaseInsensitive(scheme, "http") ||
           EqualsAsciiCaseInsensitive(scheme, "https");
}

} // namespace

bool DownloadSource(const std::string &urlStr, std::string &output)
{
    // Every return path assigns `output`, so clearing it up front is unnecessary.
    // Skipping the clear also keeps the function correct if `urlStr` and `output`
    // refer to the same object; clearing `output` would otherwise empty the URL.
    if (urlStr.empty())
    {
        output = "URL must not be empty";
        return false;
    }

    // libcurl also accepts schemes such as ftp://, but the status code query
    // below is HTTP-specific, so require HTTP or HTTPS explicitly.
    if (!HasHttpScheme(urlStr))
    {
        output = "URL must be an absolute HTTP or HTTPS URL";
        return false;
    }

    // The debugger does not link against libcurl directly; instead, the common
    // library names are probed and the runtime dependency stays optional. The
    // first failure is remembered because it corresponds to the primary name.
    constexpr std::array<const char *, 3> libraryNames{"libcurl.so.4", "libcurl.so", "libcurl.so.3"};
    std::string loadError;
    DynamicLibrary library;
    for (const char *name : libraryNames)
    {
        dlerror(); // Clear any existing error
        library.handle = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (library.handle != nullptr)
        {
            break;
        }
        if (loadError.empty())
        {
            const char *const error = dlerror();
            if (error != nullptr)
            {
                loadError = error;
            }
        }
    }
    if (library.handle == nullptr)
    {
        output = "Unable to load libcurl";
        if (!loadError.empty())
        {
            output += ": ";
            output += loadError;
        }
        return false;
    }

    CurlApi api;
    if (!LoadCurlApi(library.handle, api))
    {
        output = "Unable to load the required libcurl functions";
        return false;
    }

    const CURLcode globalInitResult = api.globalInit(CURL_GLOBAL_DEFAULT);
    if (globalInitResult != CURLE_OK)
    {
        output = "libcurl initialization failed: ";
        output += api.easyStrerror(globalInitResult);
        return false;
    }

    const struct GlobalCleanup
    {
        explicit GlobalCleanup(CurlGlobalCleanup cleanupFunction)
            : cleanup(cleanupFunction)
        {
        }

        GlobalCleanup(GlobalCleanup &&) = delete;
        GlobalCleanup(const GlobalCleanup &) = delete;
        GlobalCleanup &operator=(GlobalCleanup &&) = delete;
        GlobalCleanup &operator=(const GlobalCleanup &) = delete;

        ~GlobalCleanup() { cleanup(); }

        CurlGlobalCleanup cleanup;
    } globalCleanup(api.globalCleanup);

    CURL *const curl = api.easyInit();
    if (curl == nullptr)
    {
        output = "Unable to create a libcurl request";
        return false;
    }

    const struct EasyCleanup
    {
        EasyCleanup(CURL *handleValue, CurlEasyCleanup cleanupFunction)
            : handle(handleValue), cleanup(cleanupFunction)
        {
        }

        EasyCleanup(EasyCleanup &&) = delete;
        EasyCleanup(const EasyCleanup &) = delete;
        EasyCleanup &operator=(EasyCleanup &&) = delete;
        EasyCleanup &operator=(const EasyCleanup &) = delete;

        ~EasyCleanup() { cleanup(handle); }

        CURL *handle;
        CurlEasyCleanup cleanup;
    } easyCleanup(curl, api.easyCleanup);

    ResponseBuffer response;
    const auto setopt = [&api, curl](CURLoption option, auto value)
    {
        return api.easySetopt(curl, option, value) == CURLE_OK;
    };

    const uint32_t requestTimeoutMs = Config::GetHttpRequestTimeoutMs();
    // libcurl uses whole seconds. Round up, so a non-zero sub-second value is
    // not truncated to zero, which would disable the timeout. Zero itself is
    // kept as zero, matching libcurl's "no timeout" convention.
    const long requestTimeoutSeconds = requestTimeoutMs == 0
        ? 0L
        : static_cast<long>((static_cast<uint64_t>(requestTimeoutMs) + millisecondsPerSecond - 1) /
                            millisecondsPerSecond);

    // CURLOPT_NOSIGNAL disables the signal-based name resolution timeouts,
    // which are unsafe in multithreaded applications.
    // CURLOPT_ACCEPT_ENCODING with an empty string makes libcurl advertise
    // every compression scheme it supports and decompress the response
    // transparently, which reduces the amount of data transferred.
    // CURLOPT_HEADERFUNCTION is only used to preallocate the body buffer
    // based on the Content-Length header.
    if (!setopt(CURLOPT_URL, urlStr.c_str()) ||
#if LIBCURL_VERSION_NUM >= 0x075500 // libcurl 7.85.0
        !setopt(CURLOPT_PROTOCOLS_STR, allowedProtocolsStr) ||
        !setopt(CURLOPT_REDIR_PROTOCOLS_STR, allowedProtocolsStr) ||
#else
        !setopt(CURLOPT_PROTOCOLS, allowedProtocols) ||
        !setopt(CURLOPT_REDIR_PROTOCOLS, allowedProtocols) ||
#endif
        !setopt(CURLOPT_HEADERFUNCTION, HeaderCallback) ||
        !setopt(CURLOPT_HEADERDATA, &response) ||
        !setopt(CURLOPT_WRITEFUNCTION, WriteCallback) ||
        !setopt(CURLOPT_WRITEDATA, &response) ||
        !setopt(CURLOPT_USERAGENT, "dncdbg") ||
        !setopt(CURLOPT_ACCEPT_ENCODING, "") ||
        !setopt(CURLOPT_FOLLOWLOCATION, 1L) ||
        !setopt(CURLOPT_NOSIGNAL, 1L) ||
        !setopt(CURLOPT_CONNECTTIMEOUT, requestTimeoutSeconds) ||
        !setopt(CURLOPT_TIMEOUT, requestTimeoutSeconds))
    {
        output = "Unable to configure the libcurl request";
        return false;
    }

    const CURLcode performResult = api.easyPerform(curl);
    if (performResult != CURLE_OK)
    {
        output = "HTTP request failed: ";
        output += api.easyStrerror(performResult);
        return false;
    }

    // With CURLOPT_FOLLOWLOCATION set, the response status corresponds to the
    // final response.
    long responseCode = 0;
    if (api.easyGetinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode) != CURLE_OK)
    {
        output = "Unable to read the HTTP response status";
        return false;
    }
    if (responseCode < httpStatusOk || responseCode >= httpStatusMultipleChoices)
    {
        output = "HTTP request failed with status code " + IntegerToString(responseCode);
        return false;
    }

    output = std::move(response.data);
    return true;
}

} // namespace dncdbg

#endif // defined(FEATURE_PAL) && !defined(__APPLE__)
