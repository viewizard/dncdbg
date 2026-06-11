// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/remote_console.h"
#include "utils/logger.h"
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
        std::lock_guard<std::mutex> guard(s_wsaMutex);
        if (s_wsaRefCount == 0)
        {
            WSADATA wsaData;
            int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
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
        std::lock_guard<std::mutex> guard(s_wsaMutex);
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

bool RemoteConsoleServer::CreateListener(int port)
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        LOGE(log << "RemoteConsoleServer: socket() failed, err=" << WSAGetLastError());
        return false;
    }

    BOOL reuse = TRUE;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof(reuse)) == SOCKET_ERROR)
    {
        // Non-fatal: continue.
        LOGW(log << "RemoteConsoleServer: setsockopt(SO_REUSEADDR) failed, err=" << WSAGetLastError());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    // Bind to loopback only — this is a local server.
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

    m_listener = s;
    return true;
}

bool RemoteConsoleServer::AcceptOne()
{
    if (m_listener == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in peer{};
    int peerLen = static_cast<int>(sizeof(peer));
    SOCKET c = ::accept(m_listener, reinterpret_cast<sockaddr *>(&peer), &peerLen);
    if (c == INVALID_SOCKET)
    {
        int err = WSAGetLastError();
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

    int noDelay = 1;
    if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) == SOCKET_ERROR)
    {
        LOGW(log << "RemoteConsoleServer: setsockopt(TCP_NODELAY) failed, err=" << WSAGetLastError());
    }

    m_client = c;
    return true;
}

void RemoteConsoleServer::CloseListener()
{
    if (m_listener != INVALID_SOCKET)
    {
        ::closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }
}

void RemoteConsoleServer::CloseClient()
{
    if (m_client != INVALID_SOCKET)
    {
        ::shutdown(m_client, SD_BOTH);
        ::closesocket(m_client);
        m_client = INVALID_SOCKET;
    }
}

void RemoteConsoleServer::WakeupForShutdown()
{
    // Shutting the listener / client down causes blocking accept() / recv()
    // calls in the worker thread to fail and return immediately.
    if (m_listener != INVALID_SOCKET)
    {
        ::shutdown(m_listener, SD_BOTH);
        ::closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }
    if (m_client != INVALID_SOCKET)
    {
        ::shutdown(m_client, SD_BOTH);
    }
}

int RemoteConsoleServer::RecvBlocking(gsl::span<char> buffer)
{
    if (m_client == INVALID_SOCKET || buffer.empty())
    {
        return -1;
    }

    int n = ::recv(m_client, buffer.data(), static_cast<int>(buffer.size()), 0);
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
    if (m_client == INVALID_SOCKET)
    {
        return false;
    }

    size_t sent = 0;
    while (sent < data.size())
    {
        int chunk = ::send(m_client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
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
