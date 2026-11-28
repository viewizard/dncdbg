// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/remote_server.h"
#include "utils/logger.h"
#include <limits>
#include <ws2tcpip.h>

namespace dncdbg
{

// Static member definitions for WSA reference counting.
std::mutex RemoteServer::s_wsaMutex;
int RemoteServer::s_wsaRefCount = 0;

bool RemoteServer::PlatformStartup()
{
    try
    {
        const std::scoped_lock guard(s_wsaMutex);
        if (s_wsaRefCount == 0)
        {
            WSADATA wsaData{};
            const int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (rc != 0)
            {
                LOGE(log << "RemoteServer: WSAStartup failed, rc=" << rc);
                return false;
            }
        }
        ++s_wsaRefCount;
        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer PlatformStartup exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteServer PlatformStartup: unknown exception");
        return false;
    }
}

void RemoteServer::PlatformCleanup()
{
    try
    {
        const std::scoped_lock guard(s_wsaMutex);
        if (s_wsaRefCount > 0)
        {
            --s_wsaRefCount;
            if (s_wsaRefCount == 0)
            {
                WSACleanup();
            }
        }
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer PlatformCleanup exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteServer PlatformCleanup: unknown exception");
    }
}

bool RemoteServer::CreateListener(uint32_t port)
{
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        LOGE(log << "RemoteServer: socket() failed, err=" << WSAGetLastError());
        return false;
    }

    const BOOL reuse = TRUE;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof(reuse)) == SOCKET_ERROR)
    {
        // Non-fatal: continue.
        LOGW(log << "RemoteServer: setsockopt(SO_REUSEADDR) failed, err=" << WSAGetLastError());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    // Bind to the loopback interface only, as this is a local server.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        LOGE(log << "RemoteServer: bind() failed on port " << port
                 << ", err=" << WSAGetLastError());
        ::closesocket(s);
        return false;
    }

    if (::listen(s, 1) == SOCKET_ERROR)
    {
        LOGE(log << "RemoteServer: listen() failed, err=" << WSAGetLastError());
        ::closesocket(s);
        return false;
    }

    m_listener.store(s);
    return true;
}

bool RemoteServer::AcceptOne()
{
    const SOCKET listener = m_listener.load();
    if (listener == INVALID_SOCKET)
    {
        return false;
    }

    // The peer address is not needed, so nullptr is passed instead of an address buffer.
    const SOCKET c = ::accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET)
    {
        const int err = WSAGetLastError();
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteServer: accept() failed, err=" << err);
        }
        return false;
    }

    if (m_stopRequested.load())
    {
        ::closesocket(c);
        return false;
    }

    const int noDelay = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) == SOCKET_ERROR)
    {
        LOGW(log << "RemoteServer: setsockopt(TCP_NODELAY) failed, err=" << WSAGetLastError());
    }

    m_client.store(c);
    return true;
}

void RemoteServer::CloseListener()
{
    // Claim the descriptor so that exactly one thread releases it.
    const SOCKET listener = m_listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET)
    {
        ::closesocket(listener);
    }
}

void RemoteServer::CloseClient()
{
    SetClientConnected(false);

    // Claim the descriptor so that exactly one thread releases it.
    const SOCKET client = m_client.exchange(INVALID_SOCKET);
    if (client == INVALID_SOCKET)
    {
        return;
    }

    // Wake a recv() / send() that may be blocked on this socket.
    ::shutdown(client, SD_BOTH);

    // Wait for every in-flight RecvBlocking() to return (shutdown() above has woken them),
    // then wait for an in-flight SendToClient() to finish, so a recycled descriptor value can
    // never be used by recv() or send(). The lock order is always m_recvMutex, then m_sendMutex.
    std::unique_lock recvLock(m_recvMutex);
    m_recvCv.wait(recvLock, [this] { return m_recvInFlight == 0; });
    const std::scoped_lock sendGuard(m_sendMutex);
    ::closesocket(client);
}

void RemoteServer::WakeupForShutdown()
{
    // Closing the listener is what unblocks a pending accept() in the worker thread. The
    // claim (exchange) guarantees the descriptor is released exactly once, even if Close()
    // runs concurrently.
    const SOCKET listener = m_listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET)
    {
        ::shutdown(listener, SD_BOTH);
        ::closesocket(listener);
    }

    // Only shut the client socket down, so a blocked recv() / send() returns. The descriptor
    // itself is released by the worker thread (or by Close() after the join), therefore an
    // I/O call in flight can never observe a closed (and possibly recycled) descriptor.
    const SOCKET client = m_client.load();
    if (client != INVALID_SOCKET)
    {
        ::shutdown(client, SD_BOTH);
    }
}

int RemoteServer::RecvBlocking(gsl::span<char> buffer)
{
    if (buffer.empty())
    {
        return -1;
    }

    // The descriptor is read and the call registered under m_recvMutex. The lock is released
    // before the blocking recv(), so CloseClient() can always take it. CloseClient() shuts the
    // socket down (waking a blocked recv()) and waits for m_recvInFlight to drop to zero before
    // releasing the descriptor.
    SOCKET client = INVALID_SOCKET;
    {
        const std::scoped_lock guard(m_recvMutex);
        client = m_client.load();
        if (client == INVALID_SOCKET)
        {
            return -1;
        }
        ++m_recvInFlight;
    }

    // recv() takes an int length, so the request is clamped to that range (as in SendAll()).
    constexpr size_t maxChunk = static_cast<size_t>(std::numeric_limits<int>::max());
    const int chunkSize = static_cast<int>(buffer.size() < maxChunk ? buffer.size() : maxChunk);
    const int n = ::recv(client, buffer.data(), chunkSize, 0);
    const int err = (n == SOCKET_ERROR) ? WSAGetLastError() : 0;

    {
        const std::scoped_lock guard(m_recvMutex);
        --m_recvInFlight;
        if (m_recvInFlight == 0)
        {
            m_recvCv.notify_all();
        }
    }
    if (n == SOCKET_ERROR)
    {
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteServer: recv() failed, err=" << err);
        }
        return -1;
    }
    return n;
}

bool RemoteServer::SendAll(gsl::span<const char> data) const
{
    // The descriptor is read once and stays valid for the whole call: the caller holds
    // m_sendMutex, and CloseClient() releases the descriptor under the same mutex.
    const SOCKET client = m_client.load();
    if (client == INVALID_SOCKET)
    {
        return false;
    }

    // send() takes an int length, so large spans are sent in bounded chunks.
    constexpr size_t maxChunk = static_cast<size_t>(std::numeric_limits<int>::max());
    size_t sent = 0;
    while (sent < data.size())
    {
        const size_t remaining = data.size() - sent;
        const int chunkSize = static_cast<int>(remaining < maxChunk ? remaining : maxChunk);
        const int chunk = ::send(client, data.data() + sent, chunkSize, 0);
        if (chunk == SOCKET_ERROR)
        {
            LOGE(log << "RemoteServer: send() failed, err=" << WSAGetLastError());
            return false;
        }
        if (chunk == 0)
        {
            // Connection closed.
            return false;
        }
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

} // namespace dncdbg

#endif // _WIN32
