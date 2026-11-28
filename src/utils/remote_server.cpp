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

        // From this point on the listener exists, so Close() has to be able to release it
        // even if one of the following steps throws.
        m_initialized.store(true);
        m_stopRequested.store(false);
        m_disconnected.store(false);
        m_clientConnected.store(false);

        // Spawn the worker thread. It performs the (potentially long) accept(), so Initialize() returns immediately.
        m_workerThread = std::thread(&RemoteServer::WorkerThreadMain, this);

        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer::Initialize exception: " << e.what());
        // Release anything acquired so far (socket, platform state).
        static_cast<void>(Close());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteServer::Initialize: unknown exception");
        static_cast<void>(Close());
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

        // Signal shutdown and unblock any pending accept() or recv() calls, as well as
        // protocol readers blocked in WaitForClient().
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
                // Do not return early here: the sockets must be released regardless.
                LOGE(log << "RemoteServer::Close: join exception: " << e.what());
            }
        }

        // Final cleanup of any sockets that may still be open. Both calls are no-ops if
        // the worker thread already released the descriptors.
        CloseClient();
        CloseListener();

        if (m_initialized.exchange(false))
        {
            PlatformCleanup();
        }

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
    // An exception must not escape the thread entry point, otherwise the whole process
    // is terminated.
    try
    {
        // Wait for exactly one client to connect. If accept() failed, or shutdown was requested
        // while it was in progress, there is no session to serve and the worker thread exits.
        if (AcceptOne() && !m_stopRequested.load())
        {
            SetClientConnected(true);
            LOGI(log << "RemoteServer: client connected");

            // Unlike RemoteConsoleServer, this server does not pump the socket here:
            // the protocol stream buffers (InputStreambuf / OutputStreambuf) own the
            // connection I/O from the protocol threads. This thread only waits for
            // the connection (or the server) to end so that Close() can join it
            // cleanly once the protocol session is over.
            std::unique_lock lock(m_stateMutex);
            m_stateCv.wait(lock, [this]
            {
                return m_disconnected.load() || m_stopRequested.load();
            });
        }
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteServer worker thread exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteServer worker thread: unknown exception");
    }

    // No client can connect after this point, so wake any reader still waiting in
    // WaitForClient(). Then release the client socket, so the protocol writers do not try to
    // use it after the session is over. The claim inside CloseClient() makes this a no-op if
    // Close() already released the descriptor.
    {
        const std::scoped_lock lock(m_stateMutex);
        m_disconnected.store(true);
        m_stateCv.notify_all();
    }
    CloseClient();
}

bool RemoteServer::WaitForClient()
{
    if (!m_initialized.load())
    {
        return false;
    }

    // Fast path: a connected client is the common case for every buffer refill and for
    // every bulk read, so it should not take a lock. A stale 'true' value is harmless: the
    // socket helpers report a closed connection as an error, and the slow path below
    // re-checks the state under the lock anyway.
    if (m_clientConnected.load(std::memory_order_relaxed))
    {
        return true;
    }

    std::unique_lock lock(m_stateMutex);
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
        if (received == 0 && !m_stopRequested.load())
        {
            LOGI(log << "RemoteServer: graceful disconnect from peer");
        }
        // The connection is gone (or shutdown was requested): mark the client as
        // disconnected and let blocked readers finish instead of waiting for a
        // client that will never connect.
        {
            const std::scoped_lock lock(m_stateMutex);
            m_disconnected.store(true);
            m_stateCv.notify_all();
        }
        SetClientConnected(false);
    }
    return received;
}

bool RemoteServer::SendToClient(const char *data, size_t size)
{
    if (size == 0)
    {
        return true; // Nothing to send, so nothing failed.
    }
    if (data == nullptr || !m_initialized.load() || !m_clientConnected.load(std::memory_order_relaxed))
    {
        // No client is connected yet (or the client has already disconnected), so the data is dropped.
        return false;
    }

    const std::scoped_lock guard(m_sendMutex);

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
    // Store the flag and notify with the lock held, so a waiter that already evaluated
    // the predicate can not miss the wakeup.
    const std::scoped_lock lock(m_stateMutex);
    m_clientConnected.store(connected);
    m_stateCv.notify_all();
}

void RemoteServer::RequestStop()
{
    // Store the flags and notify with the lock held, see SetClientConnected().
    const std::scoped_lock lock(m_stateMutex);
    m_stopRequested.store(true);
    m_disconnected.store(true);
    m_stateCv.notify_all();
}

} // namespace dncdbg
