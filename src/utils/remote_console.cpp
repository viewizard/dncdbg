// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/remote_console.h"
#include "utils/logger.h"

#include <exception>
#include <utility>
#include <vector>

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

bool RemoteConsoleServer::Initialize(uint32_t port, DataCallback callback)
{
    try
    {
        if (m_initialized.load())
        {
            LOGE(log << "RemoteConsoleServer::Initialize: already initialized");
            return false;
        }

        constexpr uint32_t minPort = 1;
        constexpr uint32_t maxPort = 65535;
        if (port < minPort || port > maxPort)
        {
            LOGE(log << "RemoteConsoleServer::Initialize: invalid port " << port);
            return false;
        }

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

        // From this point on the listener exists, so Close() has to be able to release it
        // even if one of the following steps throws.
        m_initialized.store(true);
        m_stopRequested.store(false);
        m_clientConnected.store(false);

        // The callback is stored before the worker thread starts, so the worker can read it
        // without extra synchronization (thread creation orders the write). It is released
        // in Close() only after the worker has been joined. If the assignment throws
        // std::bad_alloc, the catch block below calls Close(), which releases the listener
        // and the platform state because m_initialized is already set.
        m_callback = std::move(callback);

        // Start the worker thread. It performs the (potentially long) accept() itself, so
        // Initialize() returns immediately.
        m_workerThread = std::thread(&RemoteConsoleServer::WorkerThreadMain, this);

        return true;
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer::Initialize exception: " << e.what());
        // Release anything acquired so far (socket, callback, platform state).
        static_cast<void>(Close());
        return false;
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer::Initialize: unknown exception");
        static_cast<void>(Close());
        return false;
    }
}

bool RemoteConsoleServer::SendData(gsl::span<const char> data)
{
    try
    {
        if (!m_initialized.load() || !m_clientConnected.load())
        {
            // Not initialized, or no client is connected (yet, or anymore) — silently drop.
            return false;
        }
        if (data.empty())
        {
            return true;
        }

        const std::scoped_lock guard(m_sendMutex);

        // Re-check after acquiring the lock: the client could have disconnected since the outer check.
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

        // Signal the worker thread to stop and unblock any pending accept() / recv() / send()
        // it (or the debugger output threads) may be sitting in.
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
                // Do not return early here: the sockets must be released regardless.
                LOGE(log << "RemoteConsoleServer::Close: join exception: " << e.what());
            }
        }

        // Final cleanup of any sockets that may still be open. Both calls are no-ops if
        // the worker thread already released the descriptors.
        CloseClient();
        CloseListener();

        // Release the callback (and whatever it captured) once the worker is gone.
        m_callback = nullptr;

        if (m_initialized.exchange(false))
        {
            PlatformCleanup();
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
    // An exception must not escape the thread entry point, otherwise the whole process
    // is terminated.
    try
    {
        // Wait for exactly one client to connect.
        if (!AcceptOne())
        {
            // Either shutdown was requested before a client arrived, or accept()
            // failed. Either way, the worker thread exits.
            return;
        }

        // Close() may have been requested while accept() was in progress.
        if (m_stopRequested.load())
        {
            CloseClient();
            return;
        }

        m_clientConnected.store(true);

        // The buffer lives on the heap and is reused for every chunk, so the worker
        // stack frame stays small.
        std::vector<char> buffer(ReadBufferSize);

        // Blocking read loop. Forward every chunk to the callback, which feeds it to the
        // debuggee's stdin.
        while (!m_stopRequested.load())
        {
            const int n = RecvBlocking(gsl::span<char>(buffer.data(), buffer.size()));
            if (n > 0)
            {
                if (!m_callback)
                {
                    continue;
                }

                // A throwing callback must not take the process down.
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
            else if (n == 0)
            {
                if (!m_stopRequested.load())
                {
                    LOGI(log << "RemoteConsoleServer: graceful disconnect from peer");
                }
                break;
            }
            else
            {
                // A failed recv() is expected when the server is shutting down;
                // RecvBlocking() has already logged an unexpected error.
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        LOGE(log << "RemoteConsoleServer worker thread exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "RemoteConsoleServer worker thread: unknown exception");
    }

    // Mark the client as disconnected and release the client socket here, so SendData()
    // does not try to use it after the worker exits. The claim inside CloseClient() makes
    // this a no-op if Close() already released the descriptor.
    CloseClient();
}

} // namespace dncdbg
