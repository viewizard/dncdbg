// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/remote_server.h"
#include "utils/logger.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dncdbg
{

bool RemoteServer::PlatformStartup()
{
    // Ignore SIGPIPE so a send() to a closed socket returns EPIPE instead of
    // killing the process.
    static_cast<void>(::signal(SIGPIPE, SIG_IGN));
    return true;
}

void RemoteServer::PlatformCleanup()
{
    // Nothing to do on POSIX.
}

bool RemoteServer::CreateListener(uint32_t port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        LOGE(log << "RemoteServer: socket() failed, errno=" << errno);
        return false;
    }

    int reuse = 1;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        LOGW(log << "RemoteServer: setsockopt(SO_REUSEADDR) failed, errno=" << errno);
    }

    // Make sure the listener has CLOEXEC so it doesn't leak into the debuggee child process.
    const int flags = ::fcntl(s, F_GETFD, 0);
    if (flags >= 0)
    {
        static_cast<void>(::fcntl(s, F_SETFD, static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(FD_CLOEXEC)))); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    // Bind to the loopback interface only, as this is a local server.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        LOGE(log << "RemoteServer: bind() failed on port " << port << ", errno=" << errno);
        ::close(s);
        return false;
    }

    if (::listen(s, 1) < 0)
    {
        LOGE(log << "RemoteServer: listen() failed, errno=" << errno);
        ::close(s);
        return false;
    }

    m_listener = s;
    return true;
}

bool RemoteServer::AcceptOne()
{
    if (m_listener < 0)
    {
        return false;
    }

    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);

    int c = 0;
    while (true)
    {
        c = ::accept(m_listener, reinterpret_cast<sockaddr *>(&peer), &peerLen);
        if (c >= 0)
        {
            break;
        }
        if (errno == EINTR)
        {
            if (m_stopRequested.load())
            {
                return false;
            }
            continue;
        }
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteServer: accept() failed, errno=" << errno);
        }
        return false;
    }

    if (m_stopRequested.load())
    {
        ::close(c);
        return false;
    }

    const int flags = ::fcntl(c, F_GETFD, 0);
    if (flags >= 0)
    {
        static_cast<void>(::fcntl(c, F_SETFD, static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(FD_CLOEXEC)))); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    int noDelay = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) < 0)
    {
        LOGW(log << "RemoteServer: setsockopt(TCP_NODELAY) failed, errno=" << errno);
    }

    m_client = c;
    return true;
}

void RemoteServer::CloseListener()
{
    if (m_listener >= 0)
    {
        ::close(m_listener);
        m_listener = -1;
    }
}

void RemoteServer::CloseClient()
{
    if (m_client >= 0)
    {
        ::shutdown(m_client, SHUT_RDWR);
        ::close(m_client);
        m_client = -1;
    }
}

void RemoteServer::WakeupForShutdown()
{
    // Shutting down and closing the listener makes a blocked accept() fail with
    // EBADF or EINVAL. Shutting down the client socket has the same effect on recv().
    if (m_listener >= 0)
    {
        ::shutdown(m_listener, SHUT_RDWR);
        ::close(m_listener);
        m_listener = -1;
    }
    if (m_client >= 0)
    {
        ::shutdown(m_client, SHUT_RDWR);
    }
}

int RemoteServer::RecvBlocking(gsl::span<char> buffer)
{
    if (m_client < 0 || buffer.empty())
    {
        return -1;
    }

    ssize_t n = 0;
    do
    {
        n = ::recv(m_client, buffer.data(), buffer.size(), 0);
    }
    while (n < 0 && errno == EINTR && !m_stopRequested.load());

    if (n < 0)
    {
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteServer: recv() failed, errno=" << errno);
        }
        return -1;
    }
    return static_cast<int>(n);
}

bool RemoteServer::SendAll(gsl::span<const char> data) const
{
    if (m_client < 0)
    {
        return false;
    }

    size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t chunk = ::send(m_client,
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
            LOGE(log << "RemoteServer: send() failed, errno=" << errno);
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
