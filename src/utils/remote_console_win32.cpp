// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/remote_console.h"
#include "utils/logger.h"
#include <limits>
#include <ws2tcpip.h>

namespace dncdbg
{

// Static member definitions for WSA reference counting.
std::mutex RemoteConsoleServer::s_wsaMutex;
int RemoteConsoleServer::s_wsaRefCount = 0;

bool RemoteConsoleServer::PlatformStartup()
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
                LOGE(log << "RemoteConsoleServer: WSAStartup failed, rc=" << rc);
                return false;
            }
        }
        ++s_wsaRefCount;
        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer PlatformStartup exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer PlatformStartup: unknown exception");
        return false;
    }
}

void RemoteConsoleServer::PlatformCleanup()
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
        LOGE(log << "RemoteConsoleServer PlatformCleanup exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer PlatformCleanup: unknown exception");
    }
}

bool RemoteConsoleServer::CreateListener(uint32_t port)
{
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        LOGE(log << "RemoteConsoleServer: socket() failed, err=" << WSAGetLastError());
        return false;
    }

    const BOOL reuse = TRUE;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof(reuse)) == SOCKET_ERROR)
    {
        // Non-fatal: continue.
        LOGW(log << "RemoteConsoleServer: setsockopt(SO_REUSEADDR) failed, err=" << WSAGetLastError());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    // Bind to the loopback interface only, as this is a local server.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        LOGE(log << "RemoteConsoleServer: bind() failed on port " << port
                 << ", err=" << WSAGetLastError());
        ::closesocket(s);
        return false;
    }

    if (::listen(s, 1) == SOCKET_ERROR)
    {
        LOGE(log << "RemoteConsoleServer: listen() failed, err=" << WSAGetLastError());
        ::closesocket(s);
        return false;
    }

    m_listener.store(s);
    return true;
}

bool RemoteConsoleServer::AcceptOne()
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
            LOGE(log << "RemoteConsoleServer: accept() failed, err=" << err);
        }
        return false;
    }

    if (m_stopRequested.load())
    {
        ::closesocket(c);
        return false;
    }

    const int noDelay = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) == SOCKET_ERROR)
    {
        LOGW(log << "RemoteConsoleServer: setsockopt(TCP_NODELAY) failed, err=" << WSAGetLastError());
    }

    m_client.store(c);
    return true;
}

void RemoteConsoleServer::CloseListener()
{
    // Claim the descriptor so that exactly one thread releases it.
    const SOCKET listener = m_listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET)
    {
        ::closesocket(listener);
    }
}

void RemoteConsoleServer::CloseClient()
{
    m_clientConnected.store(false);

    // Claim the descriptor so that exactly one thread releases it.
    const SOCKET client = m_client.exchange(INVALID_SOCKET);
    if (client == INVALID_SOCKET)
    {
        return;
    }

    // Wake a recv() / send() that may be blocked on this socket.
    ::shutdown(client, SD_BOTH);

    // Wait for an in-flight SendData() to leave the descriptor before releasing it, so a
    // recycled descriptor value can never be used by send().
    const std::scoped_lock guard(m_sendMutex);
    ::closesocket(client);
}

void RemoteConsoleServer::WakeupForShutdown()
{
    // Shutting the listener down causes blocking accept() calls in the worker thread to
    // fail and return immediately. The claim (exchange) guarantees the descriptor is
    // released exactly once, even if Close() runs concurrently.
    const SOCKET listener = m_listener.exchange(INVALID_SOCKET);
    if (listener != INVALID_SOCKET)
    {
        ::shutdown(listener, SD_BOTH);
        ::closesocket(listener);
    }

    // Only shut the client socket down, so a blocked recv() / send() returns. The
    // descriptor itself is released by the worker thread (or by Close() after the join),
    // therefore a recv() in flight can never observe a closed (and possibly recycled)
    // descriptor value.
    const SOCKET client = m_client.load();
    if (client != INVALID_SOCKET)
    {
        ::shutdown(client, SD_BOTH);
    }
}

int RemoteConsoleServer::RecvBlocking(gsl::span<char> buffer)
{
    // Use one stable descriptor value for the whole call. Close() only shuts the socket
    // down; the descriptor is released by the worker thread (or by Close() after the join).
    const SOCKET client = m_client.load();
    if (client == INVALID_SOCKET || buffer.empty())
    {
        return -1;
    }

    // recv() takes an int length, so the request is clamped to that range (as in SendAll()).
    constexpr size_t maxChunk = static_cast<size_t>(std::numeric_limits<int>::max());
    const int chunkSize = static_cast<int>(buffer.size() < maxChunk ? buffer.size() : maxChunk);
    const int n = ::recv(client, buffer.data(), chunkSize, 0);
    if (n == SOCKET_ERROR)
    {
        if (!m_stopRequested.load())
        {
            LOGE(log << "RemoteConsoleServer: recv() failed, err=" << WSAGetLastError());
        }
        return -1;
    }
    return n;
}

bool RemoteConsoleServer::SendAll(gsl::span<const char> data) const
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
            LOGE(log << "RemoteConsoleServer: send() failed, err=" << WSAGetLastError());
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
