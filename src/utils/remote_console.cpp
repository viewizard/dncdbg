// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/remote_console.h"
#include "utils/logger.h"

#include <array>
#include <exception>
#include <utility>

namespace dncdbg
{

RemoteConsoleServer::~RemoteConsoleServer()
{
    try
    {
        Close();
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer destructor exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer destructor: unknown exception");
    }
}

bool RemoteConsoleServer::Initialize(int port, DataCallback callback)
{
    try
    {
        if (m_initialized.load())
        {
            LOGE(log << "RemoteConsoleServer::Initialize: already initialized");
            return false;
        }

        constexpr int minPort = 0;
        constexpr int maxPort = 65535;
        if (port <= minPort || port > maxPort)
        {
            LOGE(log << "RemoteConsoleServer::Initialize: invalid port " << port);
            return false;
        }

        m_callback = std::move(callback);

        if (!PlatformStartup())
        {
            LOGE(log << "RemoteConsoleServer::Initialize: platform startup failed");
            return false;
        }

        if (!CreateListener(port))
        {
            LOGE(log << "RemoteConsoleServer::Initialize: failed to create listener on port " << port);
            PlatformCleanup();
            return false;
        }

        m_stopRequested.store(false);
        m_clientConnected.store(false);
        m_initialized.store(true);

        // Spawn worker thread. The thread itself performs the (potentially long) accept() so Initialize() returns immediately.
        m_workerThread = std::thread([this]()
        {
            try
            {
                WorkerThreadMain();
            }
            catch (const std::exception &e)
            {
                LOGE(log << "RemoteConsoleServer worker thread exception: " << e.what());
            }
            catch (...)
            {
                LOGE(log << "RemoteConsoleServer worker thread: unknown exception");
            }
        });

        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer::Initialize exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer::Initialize: unknown exception");
        return false;
    }
}

bool RemoteConsoleServer::SendData(gsl::span<const char> data)
{
    try
    {
        if (!m_initialized.load())
        {
            return false;
        }
        if (!m_clientConnected.load())
        {
            // No client connected yet (or already disconnected) — silently drop.
            return false;
        }
        if (data.empty())
        {
            return true;
        }

        const std::scoped_lock<std::mutex> guard(m_sendMutex);

        // Re-check after acquiring the lock — client could have disconnected between the outer check and here.
        if (!m_clientConnected.load())
        {
            return false;
        }

        return SendAll(data);
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer::SendData exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer::SendData: unknown exception");
        return false;
    }
}

bool RemoteConsoleServer::Close()
{
    try
    {
        if (!m_initialized.load() && !m_workerThread.joinable())
        {
            return true; // Nothing to do.
        }

        // Signal the worker thread to stop and unblock any pending accept() / recv() it is sitting in.
        m_stopRequested.store(true);
        WakeupForShutdown();

        if (m_workerThread.joinable())
        {
            try
            {
                m_workerThread.join();
            }
            catch (const std::exception &e)
            {
                LOGE(log << "RemoteConsoleServer::Close: join exception: " << e.what());
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
        LOGE(log << "RemoteConsoleServer::Close exception: " << e.what());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer::Close: unknown exception");
        return false;
    }
}

void RemoteConsoleServer::WorkerThreadMain()
{
    // Wait for exactly one client to connect.
    if (!AcceptOne())
    {
        // Either shutdown requested before a client arrived, or accept
        // failed. Either way, exit the worker thread.
        return;
    }

    m_clientConnected.store(true);

    // Blocking read loop. Forward every chunk to the callback as "stdin"
    // intent (per spec: user input fed to debuggee stdin).
    std::array<char, ReadBufferSize> buffer{};
    while (!m_stopRequested.load())
    {
        const int n = RecvBlocking(gsl::span<char>(buffer.data(), buffer.size()));
        if (n > 0)
        {
            if (m_callback)
            {
                try
                {
                    m_callback(gsl::span<char>(buffer.data(), static_cast<size_t>(n)));
                }
                catch (const std::exception &e)
                {
                    LOGE(log << "RemoteConsoleServer: callback exception: " << e.what());
                }
                catch (...)
                {
                    LOGE(log << "RemoteConsoleServer: callback unknown exception");
                }
            }
        }
        else if (n == 0)
        {
            LOGI(log << "Graceful disconnect from peer.");
            break;
        }
        else
        {
            LOGI(log << "Error or shutdown signalled.");
            break;
        }
    }

    m_clientConnected.store(false);
    // Close client socket here so SendData() won't try to use it after the
    // worker exits.
    CloseClient();
}

} // namespace dncdbg
