// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_REMOTE_CONSOLE_H
#define UTILS_REMOTE_CONSOLE_H

#include <gsl/span>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace dncdbg
{

// RemoteConsoleServer is a single-connection local TCP server.
//
// Purpose:
//   Provides a network bridge that allows a user (e.g. via netcat) to interact
//   with the debuggee program's standard streams. Bytes received from the
//   remote client are forwarded to a user-supplied callback (intended to be
//   fed to the debuggee's stdin). Bytes coming out of the debuggee (stdout /
//   stderr) can be sent back to the remote client via SendData().
//
// Threading model:
//   - Initialize() spawns a single internal worker thread and returns
//     immediately (i.e. it does NOT block waiting for the remote client).
//   - The worker thread:
//       * accepts exactly one client connection,
//       * runs a blocking recv() loop and invokes the callback per chunk,
//       * exits cleanly on disconnect or when Close() / destruction is
//         requested.
//   - Until a client actually connects, SendData() is a no-op returning false.
//   - The socket descriptors are shared between the caller's thread and the
//     worker thread, so they are atomic and are released exactly once: a
//     descriptor is claimed with an atomic exchange before shutdown / close,
//     which keeps Close() safe against a concurrent worker exit.
//
// Exception safety:
//   No public method propagates exceptions; failures are reported via bool /
//   int return codes.
//
// Lifetime:
//   Non-copyable, non-movable. The destructor will safely close sockets and
//   join the worker thread if Close() has not already been called.
//
class RemoteConsoleServer
{
  public:

    // Callback type for data arriving from the remote client.
    // The callback runs in the context of the internal worker thread.
    //   - gsl::span<char>: data buffer, valid only for the duration of the
    //                      callback invocation.
    using DataCallback = std::function<void(gsl::span<char> text)>;

    RemoteConsoleServer() = default;
    ~RemoteConsoleServer();

    // Non-copyable, non-movable.
    RemoteConsoleServer(const RemoteConsoleServer &) = delete;
    RemoteConsoleServer(RemoteConsoleServer &&) = delete;
    RemoteConsoleServer &operator=(const RemoteConsoleServer &) = delete;
    RemoteConsoleServer &operator=(RemoteConsoleServer &&) = delete;

    // Initialize the server: create a listening socket on the given local TCP
    // port, store the callback, and spawn the worker thread. Returns true on
    // success, false on any error (e.g. socket / bind / listen failure, or if
    // Initialize() was already called).
    //
    // This method is non-blocking: it returns as soon as the listener is
    // armed and the worker thread is running. Actual accept() of the single
    // client happens asynchronously inside the worker thread.
    bool Initialize(uint32_t port, DataCallback callback);

    // Send data to the connected remote client.
    // Returns true on success, false if there is no connected client or a
    // network error occurred. If no client is connected yet, the call is a
    // no-op returning false.
    bool SendData(gsl::span<const char> data);

    // Close the server: shut down sockets and join the worker thread. Safe to
    // call multiple times. Returns true on success, false on error.
    bool Close();

  private:

    // Internal helpers implemented per OS.
    // Platform-level startup / shutdown of the networking subsystem
    // (WSAStartup / WSACleanup on Windows, SIGPIPE handling on POSIX).
    static bool PlatformStartup();
    static void PlatformCleanup();

    // Create + bind + listen on the given port. Stores the listener socket.
    // Returns true on success.
    [[nodiscard]] bool CreateListener(uint32_t port);

    // Accept exactly one client. Returns true on success, false
    // on error or when shutdown was requested before a client arrived.
    [[nodiscard]] bool AcceptOne();

    // Claim and close the listener socket (if open). Safe to call from any thread and
    // more than once: only the caller that wins the atomic exchange releases the socket.
    void CloseListener();

    // Claim and close the accepted client socket (if open), after any send() in flight
    // has left the descriptor. Safe to call more than once.
    void CloseClient();

    // Shut the sockets down so blocking accept() / recv() / send() unblock with error,
    // allowing the worker thread to exit promptly. The client descriptor itself is
    // released later by the worker thread or by Close(), never while recv() is in flight.
    void WakeupForShutdown();

    // Worker thread entry point. It is OS-independent and calls the OS-specific
    // accept / recv / send helpers.
    void WorkerThreadMain();

    // OS-specific blocking recv into 'buffer'. Returns number of bytes read
    // (>0), 0 on graceful disconnect, -1 on error / shutdown.
    [[nodiscard]] int RecvBlocking(gsl::span<char> buffer);

    // OS-specific blocking send of the full span. Returns true on success.
    [[nodiscard]] bool SendAll(gsl::span<const char> data) const;

    // Size of the buffer used for blocking recv() in the worker loop. The buffer itself
    // is allocated on the heap by the worker thread and reused for every chunk, so
    // the worker stack frame stays small.
    static constexpr std::size_t ReadBufferSize = 4096;

    // The descriptors are shared between the caller's thread (Initialize(), Close(),
    // SendData()) and the worker thread, hence they are atomic. A descriptor is
    // released exactly once: the owner claims it with an atomic exchange before
    // shutdown / close, so a recycled descriptor number is never touched twice.
#ifdef _WIN32
    std::atomic<SOCKET> m_listener{INVALID_SOCKET};
    std::atomic<SOCKET> m_client{INVALID_SOCKET};
    // WSAStartup / WSACleanup are reference counted, so that multiple RemoteConsoleServer
    // instances (if ever created) share a single WSAStartup / WSACleanup pair.
    static std::mutex s_wsaMutex;
    static int s_wsaRefCount;
#elif defined(FEATURE_PAL)
    std::atomic<int> m_listener{-1};
    std::atomic<int> m_client{-1};
#endif

    // Worker thread + state.
    std::thread m_workerThread;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_clientConnected{false};
    std::mutex m_sendMutex;
    DataCallback m_callback;
};

} // namespace dncdbg

#endif // UTILS_REMOTE_CONSOLE_H
