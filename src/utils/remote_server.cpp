// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/remote_server.h"
#include "utils/logger.h"

#include <exception>

namespace dncdbg
{

RemoteServer &RemoteServer::Instance()
{
    static RemoteServer server;
    return server;
}

RemoteServer::~RemoteServer()
{
    try
    {
        Close();
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer destructor exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteServer destructor: unknown exception");
    }
}

bool RemoteServer::Initialize(uint32_t port)
{
    try
    {
        if (m_initialized.load())
        {
            LOGE(log << "RemoteServer::Initialize: already initialized");
            return false;
        }

        constexpr uint32_t minPort = 1;
        constexpr uint32_t maxPort = 65535;
        if (port < minPort || port > maxPort)
        {
            LOGE(log << "RemoteServer::Initialize: invalid port " << port);
            return false;
        }

        if (!PlatformStartup())
        {
            LOGE(log << "RemoteServer::Initialize: platform startup failed");
            return false;
        }

        if (!CreateListener(port))
        {
            LOGE(log << "RemoteServer::Initialize: failed to create listener on port " << port);
            PlatformCleanup();
            return false;
        }

        m_stopRequested.store(false);
        m_disconnected.store(false);
        m_clientConnected.store(false);
        m_initialized.store(true);

        // Spawn the worker thread. It performs the (potentially long) accept(), so Initialize() returns immediately.
        m_workerThread = std::thread([this]
        {
            try
            {
                WorkerThreadMain();
            }
            catch (const std::exception &e)
            {
                LOGE(log << "RemoteServer worker thread exception: " << e.what());
            }
            catch (...)
            {
                LOGE(log << "RemoteServer worker thread: unknown exception");
            }
        });

        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer::Initialize exception: " << e.what());
        Close();
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteServer::Initialize: unknown exception");
        Close();
        return false;
    }
}

bool RemoteServer::Close()
{
    try
    {
        if (!m_initialized.load() && !m_workerThread.joinable())
        {
            return true; // Nothing to do.
        }

        // Signal shutdown and unblock any pending accept() or recv() call, as well as
        // any protocol reader blocked in WaitForClient().
        RequestStop();
        WakeupForShutdown();

        if (m_workerThread.joinable())
        {
            try
            {
                m_workerThread.join();
            }
            catch (const std::exception &e)
            {
                LOGE(log << "RemoteServer::Close: join exception: " << e.what());
                return false;
            }
        }

        // Final cleanup of any sockets that may still be open.
        CloseClient();
        CloseListener();

        if (m_initialized.load())
        {
            PlatformCleanup();
            m_initialized.store(false);
        }
        m_clientConnected.store(false);

        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer::Close exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteServer::Close: unknown exception");
        return false;
    }
}

std::istream &RemoteServer::GetInputStream()
{
    return m_input;
}

std::ostream &RemoteServer::GetOutputStream()
{
    return m_output;
}

void RemoteServer::WorkerThreadMain()
{
    // Wait for exactly one client to connect.
    if (!AcceptOne())
    {
        // Either shutdown was requested before a client arrived, or accept()
        // failed. In both cases, the worker thread exits.
        return;
    }

    SetClientConnected(true);
    LOGI(log << "RemoteServer: client connected");

    // Unlike RemoteConsoleServer, this server does not pump the socket here:
    // the protocol stream buffers (InputStreambuf / OutputStreambuf) own the
    // connection I/O from the protocol threads. This thread only waits for
    // the connection (or the server) to end so that Close() can join it
    // cleanly once the protocol session is over.
    std::unique_lock<std::mutex> lock(m_stateMutex);
    m_stateCv.wait(lock, [this]
    {
        return m_disconnected.load() || m_stopRequested.load();
    });
}

bool RemoteServer::WaitForClient()
{
    if (!m_initialized.load())
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(m_stateMutex);
    m_stateCv.wait(lock, [this]
    {
        return m_clientConnected.load() || m_disconnected.load() || m_stopRequested.load();
    });
    return m_clientConnected.load();
}

int RemoteServer::RecvFromClient(char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return -1;
    }

    const int received = RecvBlocking(gsl::span<char>(buffer, size));
    if (received <= 0)
    {
        if (received == 0)
        {
            LOGI(log << "RemoteServer: graceful disconnect from peer");
        }
        // The connection is gone (or shutdown was requested): mark the client as
        // disconnected and let blocked readers finish instead of waiting for a
        // client that will never connect.
        m_disconnected.store(true);
        SetClientConnected(false);
    }
    return received;
}

bool RemoteServer::SendToClient(const char *data, size_t size)
{
    if (data == nullptr || size == 0 || !m_initialized.load() || !m_clientConnected.load())
    {
        // No client is connected yet (or the client has already disconnected), so the data is dropped.
        return false;
    }

    const std::scoped_lock<std::mutex> guard(m_sendMutex);

    // Check again after acquiring the lock, as the client may have disconnected
    // since the check above.
    if (!m_clientConnected.load())
    {
        return false;
    }

    return SendAll(gsl::span<const char>(data, size));
}

void RemoteServer::SetClientConnected(bool connected)
{
    m_clientConnected.store(connected);
    {
        // Take the lock before notifying so a waiter that checked the
        // predicate and is about to block cannot miss the wakeup.
        const std::scoped_lock lock(m_stateMutex);
        m_stateCv.notify_all();
    }
}

void RemoteServer::RequestStop()
{
    m_stopRequested.store(true);
    m_disconnected.store(true);
    {
        const std::scoped_lock lock(m_stateMutex);
        m_stateCv.notify_all();
    }
}

} // namespace dncdbg
