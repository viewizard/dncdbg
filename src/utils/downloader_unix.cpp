// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if defined(FEATURE_PAL) && !defined(__APPLE__)

#include "utils/downloader.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <curl/curl.h>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <utility>

namespace dncdbg
{

namespace
{

constexpr long requestTimeoutSeconds = 60L;
constexpr long httpStatusOk = 200;
constexpr long httpStatusMultipleChoices = 300;
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

// Appends the received chunk to the response buffer. Returning a value other
// than the chunk size aborts the transfer.
size_t WriteCallback(void *contents, size_t size, size_t itemCount, void *userData)
{
    if (itemCount != 0 && size > std::numeric_limits<size_t>::max() / itemCount)
    {
        return 0;
    }

    const size_t byteCount = size * itemCount;
    static_cast<std::string *>(userData)->append(static_cast<const char *>(contents), byteCount);
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

bool HasHttpScheme(const std::string &url)
{
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
    {
        return false;
    }

    std::string scheme = url.substr(0, schemeEnd);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return scheme == "http" || scheme == "https";
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

    std::string response;
    const auto setopt = [&api, curl](CURLoption option, auto value)
    {
        return api.easySetopt(curl, option, value) == CURLE_OK;
    };

    // CURLOPT_NOSIGNAL disables the signal-based name resolution timeouts,
    // which are unsafe in multithreaded applications.
    if (!setopt(CURLOPT_URL, urlStr.c_str()) ||
#if LIBCURL_VERSION_NUM >= 0x075500 // libcurl 7.85.0
        !setopt(CURLOPT_PROTOCOLS_STR, allowedProtocolsStr) ||
        !setopt(CURLOPT_REDIR_PROTOCOLS_STR, allowedProtocolsStr) ||
#else
        !setopt(CURLOPT_PROTOCOLS, allowedProtocols) ||
        !setopt(CURLOPT_REDIR_PROTOCOLS, allowedProtocols) ||
#endif
        !setopt(CURLOPT_WRITEFUNCTION, WriteCallback) ||
        !setopt(CURLOPT_WRITEDATA, &response) ||
        !setopt(CURLOPT_USERAGENT, "dncdbg") ||
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
        output = "HTTP request failed with status code " + std::to_string(responseCode);
        return false;
    }

    output = std::move(response);
    return true;
}

} // namespace dncdbg

#endif // defined(FEATURE_PAL) && !defined(__APPLE__)
