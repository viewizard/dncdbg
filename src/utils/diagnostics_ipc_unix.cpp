// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/diagnostics_ipc.h"
#include "utils/logger.h"
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <palrt.h> // S_OK, E_FAIL, FAILED, HRESULT_FROM_WIN32 (via pal.h chain)
#include <string>
#include <string_view>

namespace dncdbg::DiagnosticsIpc
{

namespace
{

// The runtime creates its default diagnostics endpoint as a Unix domain socket
// named "dotnet-diagnostic-{PID}-{disambiguation_key}-socket" in the temp
// directory. These are the constant parts of that name.
constexpr std::string_view kSocketPrefix = "dotnet-diagnostic-";
constexpr std::string_view kSocketSuffix = "-socket";
constexpr const char *kDefaultTempDir = "/tmp";

// sockaddr_un::sun_path limit on Linux/macOS.
constexpr size_t kSunPathMax = sizeof(sockaddr_un::sun_path);

// Decimal base for socket name parsing.
constexpr uint64_t kDecimalBase = 10U;

// Milliseconds / nanoseconds per second and per millisecond for clock math.
constexpr uint64_t kMsPerSec = 1000ULL;
constexpr uint64_t kNsPerMs = 1000000ULL;

// poll() error event mask; POLL* constants are plain int macros by POSIX API
// definition, hence the unsigned casts.
constexpr unsigned int kPollErrorMask = static_cast<unsigned int>(POLLERR) |
                                        static_cast<unsigned int>(POLLHUP) |
                                        static_cast<unsigned int>(POLLNVAL);

// Parses "dotnet-diagnostic-{pid}-{key}-socket" and reports whether the entry
// matches `targetPid`. Pointer-based manual parsing is used instead of sscanf
// to avoid locale / signedness pitfalls; `key` is the runtime's
// disambiguation key (process start time based), which this client does not
// need to interpret.
bool ParseSocketName(const char *name, uint32_t targetPid)
{
    const size_t nameLen = std::strlen(name);
    if (nameLen <= kSocketPrefix.size() + kSocketSuffix.size())
    {
        return false;
    }

    if (std::string_view(name, kSocketPrefix.size()) != kSocketPrefix ||
        std::string_view(name + nameLen - kSocketSuffix.size()) != kSocketSuffix)
    {
        return false;
    }

    // Parse PID: decimal digits followed by '-'.
    const char *cursor = name + kSocketPrefix.size();
    uint64_t pid = 0;
    size_t digits = 0;
    while (*cursor >= '0' && *cursor <= '9')
    {
        pid = (pid * kDecimalBase) + static_cast<uint64_t>(*cursor - '0');
        ++cursor;
        ++digits;
    }
    if (digits == 0 || *cursor != '-' || pid != static_cast<uint64_t>(targetPid))
    {
        return false;
    }
    ++cursor; // skip '-'

    // Parse disambiguation key: decimal digits, at least one, up to the suffix.
    digits = 0;
    while (*cursor >= '0' && *cursor <= '9')
    {
        ++cursor;
        ++digits;
    }

    return digits > 0 &&
           std::strlen(cursor) == kSocketSuffix.size() &&
           std::string_view(cursor) == kSocketSuffix;
}

// Returns the temp directory to scan: $TMPDIR if defined and non-empty,
// otherwise /tmp (no wordexp / tilde expansion).
std::string ResolveTempDir()
{
    const char *tmp = getenv("TMPDIR");
    if (tmp != nullptr && tmp[0] != '\0')
    {
        return tmp;
    }

    return kDefaultTempDir;
}

// Returns monotonic clock value in milliseconds.
uint64_t NowMs()
{
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }

    return (static_cast<uint64_t>(ts.tv_sec) * kMsPerSec) + (static_cast<uint64_t>(ts.tv_nsec) / kNsPerMs);
}

// Waits until the descriptor is ready for `events` or the timeout elapses.
// Returns 1 if ready, 0 on timeout, -1 on error. Retries on EINTR within the budget.
int PollReady(int fd, short events, unsigned timeoutMs)
{
    const uint64_t deadline = NowMs() + timeoutMs;

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;

    while (true)
    {
        const uint64_t now = NowMs();
        if (now >= deadline)
        {
            return 0;
        }

        const int result = ::poll(&pfd, 1, static_cast<int>(deadline - now));
        if (result > 0)
        {
            // bugprone-signed-bitwise: poll event masks are a short bit field
            // by POSIX API definition, operate on them via unsigned.
            const auto revents = static_cast<unsigned int>(pfd.revents);
            if ((revents & static_cast<unsigned int>(events)) != 0)
            {
                return 1;
            }
            if ((revents & kPollErrorMask) != 0)
            {
                return -1;
            }
            continue; // spurious wakeup
        }
        if (result == 0)
        {
            return 0;
        }
        if (errno == EINTR)
        {
            continue;
        }

        return -1;
    }
}

} // unnamed namespace

// Endpoint discovery: scan the temp directory for
// "dotnet-diagnostic-{pid}-{key}-socket" entries matching `pid` and return the
// absolute path of the match. readdir order is not specified, so the match
// which is the first in lexical order is used; multiple matches (should not
// happen for one live PID) produce a warning. The path length is checked
// against the sockaddr_un::sun_path limit and the entry must be a socket.
HRESULT IpcEndpointResolve(uint32_t pid, std::string &outEndpoint)
{
    outEndpoint.clear();

    const std::string tempDir = ResolveTempDir();

    DIR *dir = opendir(tempDir.c_str());
    if (dir == nullptr)
    {
        LOGE(log << "DiagnosticsIpc: failed to open temp directory '" << tempDir << "', errno=" << errno);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    std::string match;
    std::string matchName;
    bool multiple = false;
    while (const dirent *entry = readdir(dir))
    {
        // cppcoreguidelines-pro-bounds-array-to-pointer-decay: d_name is a
        // fixed-size char array by POSIX API definition.
        const char *name = entry->d_name; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (!ParseSocketName(name, pid))
        {
            continue;
        }

        if (!matchName.empty() && matchName.compare(name) <= 0)
        {
            multiple = true;
            continue;
        }

        if (!matchName.empty())
        {
            multiple = true;
        }

        matchName.assign(name);
        match = tempDir;
        if (match.back() != '/')
        {
            match += '/';
        }
        match += name;
    }
    static_cast<void>(closedir(dir));

    if (match.empty())
    {
        LOGD(log << "DiagnosticsIpc: no diagnostics socket found for PID " << pid << " in '" << tempDir << "'");
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    if (multiple)
    {
        LOGW(log << "DiagnosticsIpc: multiple diagnostics sockets found for PID " << pid
                 << ", using the first in lexical order: " << match);
    }

    if (match.size() >= kSunPathMax)
    {
        LOGE(log << "DiagnosticsIpc: socket path is too long (" << match.size() << "): " << match);
        return E_FAIL;
    }

    struct stat sb{};
    if (stat(match.c_str(), &sb) != 0 || !S_ISSOCK(sb.st_mode))
    {
        LOGD(log << "DiagnosticsIpc: diagnostics endpoint is not a socket file: " << match);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    outEndpoint = match;
    return S_OK;
}

// Connects an AF_UNIX SOCK_STREAM socket to `endpoint`. A non-blocking connect
// with a kConnectTimeoutMs budget is used: fcntl(O_NONBLOCK) -> connect ->
// poll(POLLOUT) -> getsockopt(SO_ERROR), then blocking mode is restored (the
// socket is never left non-blocking afterwards). EINTR is handled on connect.
HRESULT IpcStreamOpen(const std::string &endpoint, IpcHandle &outHandle)
{
    outHandle = kInvalidIpcHandle;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOGE(log << "DiagnosticsIpc: socket() failed, errno=" << errno);
        return E_FAIL;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // cppcoreguidelines-pro-bounds-array-to-pointer-decay: sockaddr_un::sun_path
    // is a fixed-size char array by POSIX API definition.
    endpoint.copy(addr.sun_path, sizeof(addr.sun_path) - 1); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0'; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

    const int flags = ::fcntl(fd, F_GETFL, 0); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (flags < 0)
    {
        LOGE(log << "DiagnosticsIpc: fcntl(F_GETFL) failed, errno=" << errno);
        static_cast<void>(::close(fd));
        return E_FAIL;
    }

    // cppcoreguidelines-pro-type-vararg: fcntl is a POSIX variadic function,
    // this is its documented one-argument form. bugprone-signed-bitwise: the
    // file status flags are an int bit mask by POSIX API definition.
    if (::fcntl(fd, F_SETFL, static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(O_NONBLOCK))) < 0) // NOLINT(cppcoreguidelines-pro-type-vararg,bugprone-signed-bitwise)
    {
        LOGE(log << "DiagnosticsIpc: fcntl(O_NONBLOCK) failed, errno=" << errno);
        static_cast<void>(::close(fd));
        return E_FAIL;
    }

    const int connectResult = ::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (connectResult < 0 && errno != EINPROGRESS && errno != EINTR)
    {
        // Note: EINTR on a non-blocking socket means the connection attempt
        // continues in the background, handle it like EINPROGRESS below.
        LOGD(log << "DiagnosticsIpc: connect() failed to '" << endpoint << "', errno=" << errno);
        static_cast<void>(::fcntl(fd, F_SETFL, flags)); // NOLINT(cppcoreguidelines-pro-type-vararg) restore blocking mode
        static_cast<void>(::close(fd));
        return E_FAIL;
    }

    if (connectResult < 0)
    {
        // EINPROGRESS (or interrupted EINTR): wait for writability within the remaining budget.
        const int pollResult = PollReady(fd, POLLOUT, kConnectTimeoutMs);
        if (pollResult <= 0)
        {
            LOGD(log << "DiagnosticsIpc: connect() "
                     << (pollResult == 0 ? "timed out" : "failed") << " to '" << endpoint << "'");
            static_cast<void>(::fcntl(fd, F_SETFL, flags)); // NOLINT(cppcoreguidelines-pro-type-vararg) restore blocking mode
            static_cast<void>(::close(fd));
            return E_FAIL;
        }

        int soError = 0;
        socklen_t soErrorLen = sizeof(soError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soErrorLen) < 0 || soError != 0)
        {
            LOGD(log << "DiagnosticsIpc: connect() failed to '" << endpoint << "', SO_ERROR="
                     << (soErrorLen == sizeof(soError) ? soError : errno));
            static_cast<void>(::fcntl(fd, F_SETFL, flags)); // NOLINT(cppcoreguidelines-pro-type-vararg) restore blocking mode
            static_cast<void>(::close(fd));
            return E_FAIL;
        }
    }

    // Restore blocking mode before any further I/O.
    if (::fcntl(fd, F_SETFL, flags) < 0) // NOLINT(cppcoreguidelines-pro-type-vararg)
    {
        LOGE(log << "DiagnosticsIpc: fcntl(restore blocking) failed, errno=" << errno);
        static_cast<void>(::close(fd));
        return E_FAIL;
    }

    outHandle = fd;
    return S_OK;
}

// Closes the socket descriptor; -1 is normalized to a no-op.
void IpcStreamClose(IpcHandle handle)
{
    if (handle != kInvalidIpcHandle)
    {
        static_cast<void>(::close(handle));
    }
}

// read() loop until exactly `length` bytes are received: handles partial
// reads, retries on EINTR, polls with a kIoTimeoutMs budget on EAGAIN /
// EWOULDBLOCK, treats EOF as an error.
HRESULT IpcStreamReadAll(IpcHandle handle, uint8_t *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        const ssize_t bytesRead = ::read(handle, buffer + total, length - total);
        if (bytesRead > 0)
        {
            total += static_cast<size_t>(bytesRead);
            continue;
        }

        if (bytesRead == 0)
        {
            LOGE(log << "DiagnosticsIpc: unexpected EOF while reading, got " << total << " of " << length);
            return E_FAIL;
        }

        if (errno == EINTR)
        {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if (PollReady(handle, POLLIN, kIoTimeoutMs) <= 0)
            {
                LOGE(log << "DiagnosticsIpc: read() timed out, errno=" << errno);
                return E_FAIL;
            }
            continue;
        }

        LOGE(log << "DiagnosticsIpc: read() failed, errno=" << errno);
        return E_FAIL;
    }

    return S_OK;
}

// write() loop until exactly `length` bytes are sent: handles partial writes,
// retries on EINTR, polls with a kIoTimeoutMs budget on EAGAIN / EWOULDBLOCK.
HRESULT IpcStreamWriteAll(IpcHandle handle, const uint8_t *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        const ssize_t bytesWritten = ::write(handle, buffer + total, length - total);
        if (bytesWritten > 0)
        {
            total += static_cast<size_t>(bytesWritten);
            continue;
        }

        if (bytesWritten < 0 && errno == EINTR)
        {
            continue;
        }

        if (bytesWritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (PollReady(handle, POLLOUT, kIoTimeoutMs) <= 0)
            {
                LOGE(log << "DiagnosticsIpc: write() timed out, errno=" << errno);
                return E_FAIL;
            }
            continue;
        }

        LOGE(log << "DiagnosticsIpc: write() failed, errno=" << errno);
        return E_FAIL;
    }

    return S_OK;
}

} // namespace dncdbg::DiagnosticsIpc

#endif // FEATURE_PAL
