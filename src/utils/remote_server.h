// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_REMOTE_SERVER_H
#define UTILS_REMOTE_SERVER_H

#include <gsl/span>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace dncdbg
{

// RemoteServer is a single-connection local TCP server that exposes the remote
// client's socket as C++ standard streams.
//
// Purpose:
//   Provides the I/O backend for the DAP protocol engine. When the debugger is
//   started with the --server command-line option (see Config::IsServerMode()),
//   ReadData() pulls protocol requests from GetInputStream() and EmitMessage()
//   pushes protocol responses / events into GetOutputStream() instead of using
//   std::cin / std::cout. The remote client is accepted on a loopback TCP port
//   (see Config::GetServerPort()).
//
// Threading model:
//   - Initialize() spawns a single internal worker thread and returns
//     immediately (i.e. it does not block waiting for the remote client).
//   - The worker thread accepts exactly one client connection and then stays
//     alive (so Close() can join it) until the connection ends or shutdown is
//     requested.
//   - Unlike RemoteConsoleServer, the connection I/O is not pumped by the
//     worker thread: the protocol stream buffers (InputStreambuf /
//     OutputStreambuf) perform blocking recv() / send() directly from the
//     protocol threads (the DAP command loop thread reads, the DAP output
//     threads write under the DAP output mutex).
//   - The stream buffers keep the syscall count per protocol message at one:
//     the input side refills its buffer with a single recv() and reads large
//     payloads straight into the caller's buffer, the output side accumulates
//     everything written between two flushes and sends it in one call.
//   - Until a client connects, reads from the input stream block (waiting for
//     the client), while writes to the output stream are silently dropped.
//   - When the remote client disconnects (or the server shuts down), the input
//     stream reports EOF, so the protocol engine can initiate a forced
//     disconnect.
//
// Exception safety:
//   No public method propagates exceptions; failures are reported via bool
//   return codes, and stream errors are reported via the standard stream
//   state (EOF on the input stream, silent drop on the output stream).
//
// Lifetime:
//   Accessed via Instance() (function-local static). Non-copyable,
//   non-movable. The destructor closes the sockets and joins the worker thread
//   if Close() has not been called yet.
//
class RemoteServer
{
  public:

    RemoteServer() = default;
    ~RemoteServer();

    // Non-copyable, non-movable.
    RemoteServer(const RemoteServer &) = delete;
    RemoteServer(RemoteServer &&) = delete;
    RemoteServer &operator=(const RemoteServer &) = delete;
    RemoteServer &operator=(RemoteServer &&) = delete;

    // Process-wide instance: the protocol engine (ReadData() / EmitMessage())
    // and the startup code in main.cpp must all see the same server state.
    static RemoteServer &Instance();

    // Initialize the server: create a listening socket on the given local TCP
    // port and spawn the worker thread. Returns true on success, false on any
    // error (e.g. socket / bind / listen failure, or if Initialize() was
    // already called).
    //
    // This method is non-blocking: it returns as soon as the listener is
    // armed and the worker thread is running. Actual accept() of the single
    // client happens asynchronously in the worker thread.
    bool Initialize(uint32_t port);

    // Close the server: shut down sockets and join the worker thread. Safe to
    // call multiple times. Returns true on success, false on error.
    bool Close();

    // Protocol input stream (DAP requests). Reads block until a client is
    // connected; EOF is reported once the connection is gone or the server is
    // shutting down.
    std::istream &GetInputStream();

    // Protocol output stream (DAP responses and events). Callers are expected
    // to flush explicitly (EmitMessage() does). Data written before a client
    // connects or after the connection is lost is silently dropped.
    std::ostream &GetOutputStream();

  private:

    // ---------- Protocol stream buffers ----------

    // The input and the output buffer are used by different protocol threads, so each one
    // is aligned to a cache line to make sure they never share one. 64 bytes matches most
    // current CPUs; a different value would only affect performance, not correctness.
    static constexpr size_t cacheLineSize = 64;

    // Adapts the accepted client socket into a readable std::streambuf for
    // the protocol input stream. See the RemoteServer class comment for the
    // blocking / EOF semantics.
    class InputStreambuf final : public std::streambuf
    {
      public:

        explicit InputStreambuf(RemoteServer *server) noexcept
            : m_server(server)
        {
            // Start with an empty get area; underflow() fills it from the socket.
            setg(m_buffer.data(), m_buffer.data(), m_buffer.data());
        }

      protected:

        int_type underflow() override
        {
            if (gptr() < egptr())
            {
                return traits_type::to_int_type(*gptr());
            }
            if (!FillBuffer())
            {
                return traits_type::eof();
            }
            return traits_type::to_int_type(*gptr());
        }

        // Bulk reads (std::istream::read(), e.g. for a protocol payload) can ask for far more
        // than the internal buffer holds. Whatever is already buffered is served without a
        // syscall, and a large remainder is received straight into the caller's buffer, so such
        // a payload is neither copied twice nor fetched through the small buffer chunk by chunk.
        std::streamsize xsgetn(char *s, std::streamsize count) override
        {
            if (s == nullptr || count <= 0)
            {
                return 0;
            }

            std::streamsize copied = 0;

            // Serve the already received bytes first.
            const std::streamsize buffered = egptr() - gptr();
            if (buffered > 0)
            {
                const auto len = std::min<std::streamsize>(buffered, count);
                traits_type::copy(s, gptr(), static_cast<size_t>(len));
                gbump(static_cast<int>(len));
                copied = len;
                count -= len;
                if (count == 0)
                {
                    return copied;
                }
            }

            // A large remainder bypasses the internal buffer entirely.
            if (count >= static_cast<std::streamsize>(BufferSize))
            {
                // Same as in underflow(): wait for the client instead of reporting EOF,
                // since recv() on a connection that has not been accepted yet is meaningless.
                if (!m_server->WaitForClient())
                {
                    return copied;
                }
                while (count >= static_cast<std::streamsize>(BufferSize))
                {
                    const int received = m_server->RecvFromClient(s + copied, static_cast<size_t>(count));
                    if (received <= 0)
                    {
                        // Disconnect, network error or shutdown: report what was read so far;
                        // the next call reports EOF to the protocol engine.
                        return copied;
                    }
                    copied += received;
                    count -= received;
                }
            }

            // Top up the internal buffer for the (small) tail.
            while (count > 0)
            {
                if (gptr() == egptr() && traits_type::eq_int_type(underflow(), traits_type::eof()))
                {
                    break;
                }
                const auto len = std::min<std::streamsize>(egptr() - gptr(), count);
                traits_type::copy(s + copied, gptr(), static_cast<size_t>(len));
                gbump(static_cast<int>(len));
                copied += len;
                count -= len;
            }
            return copied;
        }

      private:

        // Refills the get area from the socket. Returns false on EOF / disconnect.
        [[nodiscard]] bool FillBuffer()
        {
            if (!m_server->WaitForClient())
            {
                return false;
            }
            const int received = m_server->RecvFromClient(m_buffer.data(), BufferSize);
            if (received <= 0)
            {
                // Graceful disconnect, network error or shutdown: report EOF so
                // the protocol engine can initiate a forced disconnect.
                return false;
            }
            setg(m_buffer.data(), m_buffer.data(), m_buffer.data() + received);
            return true;
        }

        RemoteServer *m_server;
        static constexpr size_t BufferSize = 4096;
        // This buffer is only written by the protocol reader (DAP command loop). The output
        // buffer is only written by the protocol writers.
        alignas(cacheLineSize) std::array<char, BufferSize> m_buffer{};
    };

    // Adapts the accepted client socket into a writable std::streambuf for
    // the protocol output stream. Data written while no client is connected
    // is silently dropped (mirroring RemoteConsoleServer::SendData()), so the
    // stream itself never enters the error state.
    class OutputStreambuf final : public std::streambuf
    {
      public:

        explicit OutputStreambuf(RemoteServer *server) noexcept
            : m_server(server)
        {
            setp(m_buffer.data(), m_buffer.data() + m_buffer.size());
        }

      protected:

        int_type overflow(int_type ch) override
        {
            if (traits_type::eq_int_type(ch, traits_type::eof()))
            {
                return traits_type::not_eof(ch);
            }
            // The buffer is full: preserve ordering by sending it out first.
            FlushBuffered();
            *pptr() = traits_type::to_char_type(ch);
            pbump(1);
            return traits_type::not_eof(ch);
        }

        int sync() override
        {
            // Errors are reported as a silent drop (see the class comment), so
            // sync never fails.
            FlushBuffered();
            return 0;
        }

        // Writes are accumulated and leave the buffer with a single send() per flush (the
        // protocol engine flushes once per message), so the message header and body are sent
        // together. A payload that can never fit the buffer is sent directly instead of being
        // copied through it.
        std::streamsize xsputn(const char *s, std::streamsize count) override
        {
            if (count <= 0)
            {
                return 0;
            }

            if (count > epptr() - pptr())
            {
                // Preserve ordering: anything buffered goes out first.
                FlushBuffered();
                if (count >= static_cast<std::streamsize>(BufferSize))
                {
                    // No client is connected or the connection was lost: the data is
                    // dropped, but the stream stays healthy.
                    static_cast<void>(m_server->SendToClient(s, static_cast<size_t>(count)));
                    return count;
                }
            }

            // After the flush above the whole buffer is available again.
            traits_type::copy(pptr(), s, static_cast<size_t>(count));
            pbump(static_cast<int>(count));
            return count;
        }

      private:

        void FlushBuffered()
        {
            // pptr() is never behind pbase(), and the pending size never exceeds
            // BufferSize (4096), so it always fits in an int.
            const int pending = static_cast<int>(pptr() - pbase());
            if (pending == 0)
            {
                return;
            }
            static_cast<void>(m_server->SendToClient(pbase(), static_cast<size_t>(pending)));
            // Always reset the put area: dropped data is gone either way.
            pbump(-pending);
        }

        RemoteServer *m_server;
        static constexpr size_t BufferSize = 4096;
        // See InputStreambuf: this buffer is only written by the protocol writers.
        alignas(cacheLineSize) std::array<char, BufferSize> m_buffer{};
    };

    // ---------- Server internals ----------

    // Blocks until a client is connected or the connection is gone / the
    // server is shutting down. Returns true if a client is connected.
    // Once a client is connected this is a lock-free check, so the stream
    // buffers may call it before every refill.
    [[nodiscard]] bool WaitForClient();

    // Blocking recv from the connected client. On EOF / error marks the
    // connection as terminated and wakes all blocked readers. Returns the number
    // of bytes read (>0), 0 on graceful disconnect, -1 on error / shutdown.
    [[nodiscard]] int RecvFromClient(char *buffer, size_t size);

    // Sends the full buffer to the connected client. Returns false (dropping
    // the data) when no client is connected or a network error occurred.
    [[nodiscard]] bool SendToClient(const char *data, size_t size);

    // Updates the client connection state and wakes all blocked readers.
    void SetClientConnected(bool connected);

    // Marks the server as stopped and wakes all blocked readers.
    void RequestStop();

    // Helpers that are implemented separately for each OS.
    // Platform-level startup and shutdown of the networking subsystem
    // (WSAStartup / WSACleanup on Windows, SIGPIPE handling on POSIX).
    [[nodiscard]] static bool PlatformStartup();
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

    // Claim and close the accepted client socket (if open), once any in-flight send() has
    // finished with the descriptor. Safe to call more than once: only the caller that wins the
    // atomic exchange releases the socket.
    void CloseClient();

    // Unblock a blocking accept() / recv() / send() with an error, so the worker thread (and
    // any blocked protocol reader) can exit promptly. The listener is closed here, as that is
    // what wakes accept(). The client descriptor is only shut down; it is released later by the
    // worker thread or by Close(), never while recv() / send() may still be using it.
    void WakeupForShutdown();

    // Worker thread entry point: accepts the single client and then waits for
    // the connection (or the server) to end.
    void WorkerThreadMain();

    // OS-specific blocking recv into the given buffer. Returns number of bytes read
    // (>0), 0 on graceful disconnect, -1 on error / shutdown.
    [[nodiscard]] int RecvBlocking(gsl::span<char> buffer);

    // OS-specific blocking send of the full span. Returns true on success.
    [[nodiscard]] bool SendAll(gsl::span<const char> data) const;

    // ---------- State ----------

    // The stream buffers are declared before the streams that use them, because
    // members are initialized in declaration order. Passing 'this' to the buffers
    // is valid here, as the object is already under construction. The buffers are
    // cache-line aligned, so they are declared first to keep their alignment padding
    // away from the other members.
    InputStreambuf m_inputBuf{this};
    OutputStreambuf m_outputBuf{this};

    // Worker thread.
    std::thread m_workerThread;

    // Wakes blocked protocol readers on connect, disconnect and shutdown. It is guarded by
    // m_stateMutex: the predicate flags are always stored while that mutex is held, so a
    // waiter that already evaluated the predicate cannot miss a wakeup.
    std::condition_variable m_stateCv;
    std::mutex m_stateMutex;

    // Serializes send() access to the client socket. CloseClient() releases the
    // descriptor under this lock as well, so a send() in flight can never observe a
    // closed (and possibly recycled) descriptor number.
    std::mutex m_sendMutex;

    // Guards the in-flight bookkeeping of recv() on the client socket. recv() itself is
    // never called under this mutex: RecvBlocking() registers the call (m_recvInFlight),
    // and CloseClient() waits on m_recvCv until every registered call has returned, so a
    // descriptor is never released while recv() may still use it.
    std::mutex m_recvMutex;
    std::condition_variable m_recvCv;
    std::size_t m_recvInFlight = 0;

    std::ostream m_output{&m_outputBuf};
    std::istream m_input{&m_inputBuf};

    // These are atomics because Close() may touch the sockets from the main thread while
    // the worker and protocol threads may still be using them. A descriptor is released exactly
    // once: the owner claims it with an atomic exchange before shutdown / close, so a
    // recycled descriptor number is never touched twice.
#ifdef _WIN32
    std::atomic<SOCKET> m_listener{INVALID_SOCKET};
    std::atomic<SOCKET> m_client{INVALID_SOCKET};
    // WSAStartup / WSACleanup are reference-counted, so repeated Initialize() /
    // Close() cycles share a single WSAStartup / WSACleanup pair.
    static std::mutex s_wsaMutex;
    static int s_wsaRefCount;
#elif defined(FEATURE_PAL)
    std::atomic<int> m_listener{-1};
    std::atomic<int> m_client{-1};
#endif

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_clientConnected{false};
    // Set when the connection is gone (or shutdown was requested); used to
    // unblock WaitForClient() waiters instead of blocking them forever.
    std::atomic<bool> m_disconnected{false};
};

} // namespace dncdbg

#endif // UTILS_REMOTE_SERVER_H
