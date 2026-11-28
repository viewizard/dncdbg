// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/remote_console.h"
#include "utils/logger.h"

#include <cerrno>
#include <csignal>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#if !defined(__linux__)
#include <fcntl.h>
#endif

namespace dncdbg
{

bool RemoteConsoleServer::PlatformStartup()
{
    // Ignore SIGPIPE so a send() to a closed socket returns EPIPE instead of
    // killing the process. Done once for the whole process.
    static std::once_flag sigpipeOnce;
    std::call_once(sigpipeOnce, []
    {
        static_cast<void>(::signal(SIGPIPE, SIG_IGN));
    });
    return true;
}

void RemoteConsoleServer::PlatformCleanup()
{
    // Nothing to do on POSIX.
}

bool RemoteConsoleServer::CreateListener(uint32_t port)
{
#if defined(__linux__)
    // Close-on-exec is applied atomically, so the listener cannot leak into a debuggee
    // child process spawned concurrently by another thread.
    const int s = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (s < 0)
    {
        LOGE(log << "RemoteConsoleServer: socket() failed, errno=" << errno);
        return false;
    }

    const int reuse = 1;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        LOGW(log << "RemoteConsoleServer: setsockopt(SO_REUSEADDR) failed, errno=" << errno);
    }

#if !defined(__linux__)
    // Set FD_CLOEXEC so the listener does not leak into the debuggee child process.
    const int flags = ::fcntl(s, F_GETFD, 0);
    if (flags >= 0)
    {
        static_cast<void>(::fcntl(s, F_SETFD, static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(FD_CLOEXEC)))); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    // Bind to the loopback interface only, as this is a local server.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        LOGE(log << "RemoteConsoleServer: bind() failed on port " << port << ", errno=" << errno);
        ::close(s);
        return false;
    }

    if (::listen(s, 1) < 0)
    {
        LOGE(log << "RemoteConsoleServer: listen() failed, errno=" << errno);
        ::close(s);
        return false;
    }

    m_listener.store(s);
    return true;
}

bool RemoteConsoleServer::AcceptOne()
{
    const int listener = m_listener.load();
    if (listener < 0)
    {
        return false;
    }

    int c = -1;
    do
    {
#if defined(__linux__)
        // Close-on-exec is applied atomically to the accepted socket as well. The peer
        // address is not needed, so nullptr is passed instead of an address buffer.
        c = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
#else
        c = ::accept(listener, nullptr, nullptr);
#endif
    }
    while (c < 0 && errno == EINTR && !m_stopRequested.load());

    if (c < 0)
    {
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteConsoleServer: accept() failed, errno=" << errno);
        }
        return false;
    }

    if (m_stopRequested.load())
    {
        ::close(c);
        return false;
    }

#if !defined(__linux__)
    const int flags = ::fcntl(c, F_GETFD, 0);
    if (flags >= 0)
    {
        static_cast<void>(::fcntl(c, F_SETFD, static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(FD_CLOEXEC)))); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }
#endif

    const int noDelay = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) < 0)
    {
        LOGW(log << "RemoteConsoleServer: setsockopt(TCP_NODELAY) failed, errno=" << errno);
    }

    m_client.store(c);
    return true;
}

void RemoteConsoleServer::CloseListener()
{
    // Claim the descriptor so that exactly one thread releases it.
    const int listener = m_listener.exchange(-1);
    if (listener >= 0)
    {
        ::close(listener);
    }
}

void RemoteConsoleServer::CloseClient()
{
    m_clientConnected.store(false);

    // Claim the descriptor so that exactly one thread releases it.
    const int client = m_client.exchange(-1);
    if (client < 0)
    {
        return;
    }

    // Wake a recv() / send() that may be blocked on this socket.
    ::shutdown(client, SHUT_RDWR);

    // Wait for an in-flight SendData() to leave the descriptor before releasing it, so a
    // recycled descriptor number can never be used by send().
    const std::scoped_lock guard(m_sendMutex);
    ::close(client);
}

void RemoteConsoleServer::WakeupForShutdown()
{
    // Shutting down and closing the listener causes a blocked accept() to fail and
    // return. The claim (exchange) guarantees the descriptor is released exactly once,
    // even if Close() runs concurrently.
    const int listener = m_listener.exchange(-1);
    if (listener >= 0)
    {
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
    }

    // Only shut the client socket down, so a blocked recv() / send() returns. The
    // descriptor itself is released by the worker thread (or by Close() after the join),
    // therefore a recv() in flight can never observe a closed (and possibly recycled)
    // descriptor number.
    const int client = m_client.load();
    if (client >= 0)
    {
        ::shutdown(client, SHUT_RDWR);
    }
}

int RemoteConsoleServer::RecvBlocking(gsl::span<char> buffer)
{
    // Use one stable descriptor value for the whole call. Close() only shuts the socket
    // down; the descriptor is released by the worker thread (or by Close() after the join).
    const int client = m_client.load();
    if (client < 0 || buffer.empty())
    {
        return -1;
    }

    ssize_t n = 0;
    do
    {
        n = ::recv(client, buffer.data(), buffer.size(), 0);
    }
    while (n < 0 && errno == EINTR && !m_stopRequested.load());

    if (n < 0)
    {
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteConsoleServer: recv() failed, errno=" << errno);
        }
        return -1;
    }
    return static_cast<int>(n);
}

bool RemoteConsoleServer::SendAll(gsl::span<const char> data) const
{
    // The descriptor is read once and stays valid for the whole call: the caller holds
    // m_sendMutex, and CloseClient() releases the descriptor under the same mutex.
    const int client = m_client.load();
    if (client < 0)
    {
        return false;
    }

    size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t chunk = ::send(client,
                                     data.data() + sent,
                                     data.size() - sent,
#ifdef MSG_NOSIGNAL
                                     MSG_NOSIGNAL
#else
                                     0
#endif
                                     );
        if (chunk < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            LOGE(log << "RemoteConsoleServer: send() failed, errno=" << errno);
            return false;
        }
        if (chunk == 0)
        {
            return false;
        }
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

} // namespace dncdbg

#endif // FEATURE_PAL
