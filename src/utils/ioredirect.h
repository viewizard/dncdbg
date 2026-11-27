// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_IOREDIRECT_H
#define UTILS_IOREDIRECT_H

#include "utils/function_ref.h"
#include <gsl/span>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace dncdbg
{

// IORedirect class provides I/O redirection for a child process.
//
// It creates unnamed pipes for stdin, stdout, and stderr, then allows launching
// a child process with those pipes substituted for the standard file descriptors.
// After the child process is launched, worker threads read from the child's stdout
// and stderr pipes and deliver data via a callback. The debugger can also write
// data to the child's stdin pipe.
//
// The worker threads never block uninterruptibly: each redirection session owns a
// wakeup primitive (a self-pipe on POSIX, a manual-reset event on Windows) that
// makes a pending wait fail. Therefore Reset(), Exec(), and the destructor stop the
// worker threads even when the child process keeps its output pipes open forever.
//
// Usage:
//   1. Construct IORedirect with an OutputCallback.
//   2. Call Exec() with a lambda that creates the child process.
//      Inside the lambda, stdin/stdout/stderr are redirected to the pipes.
//      Exec() may be called multiple times: each call finishes the previous
//      session (if any) and starts a new one with a fresh set of pipes.
//   3. The OutputCallback is called from worker threads when the child writes
//      to stdout or stderr.
//   4. Call WriteStdin() to send data to the child's stdin.
//   5. The next Exec() call, Reset(), or destruction stops worker threads and
//      closes all pipes.
//
class IORedirect
{
  public:

    // Stream type identifier for stdout and stderr.
    enum class StreamType : uint8_t
    {
        Stdout = 0,
        Stderr = 1
    };

    // Callback type for receiving output from the child process.
    // Called from worker threads when data is available on stdout or stderr.
    //   - StreamType: identifies whether data came from stdout or stderr.
    //   - gsl::span<char>: the data buffer (valid only during the callback).
    using OutputCallback = std::function<void(StreamType, gsl::span<char>)>;

    // Construct IORedirect with the given output callback.
    // The callback will be invoked from worker threads when the child process
    // writes to stdout or stderr. Pipes are created for each redirection
    // session started by Exec().
    explicit IORedirect(OutputCallback callback);

    // Non-copyable, non-movable.
    IORedirect(IORedirect &&) = delete;
    IORedirect(const IORedirect &) = delete;
    IORedirect &operator=(IORedirect &&) = delete;
    IORedirect &operator=(const IORedirect &) = delete;

    // Destructor stops worker threads and closes all pipe handles.
    ~IORedirect();

    // Execute a function with stdin/stdout/stderr redirected to the internal pipes.
    //
    // The provided callback `func` should create a child process (e.g., via
    // CreateProcessForLaunch). During the callback execution, the standard file
    // descriptors (stdin/stdout/stderr) are temporarily replaced with the pipe
    // endpoints so the child process inherits them.
    //
    // After the callback returns, the child-side pipe ends are closed,
    // and worker threads begin reading from the child's stdout and stderr.
    //
    // Each call starts a new redirection session: the previous session (if any)
    // is finished first (worker threads are stopped, old pipes are closed), and
    // a fresh set of pipes is created. If the session cannot be started, `func`
    // is not called and the function returns.
    //
    // If `func` throws, the standard file descriptors are restored and the session
    // is released before the exception is propagated.
    void Exec(FunctionRef<void()> func);

    // Write data to the child process's stdin pipe.
    //
    // The data from `data` is written to the stdin pipe.
    // Returns the number of bytes actually written, or -1 on error.
    // Returns 0 if the stdin pipe has been closed.
    int WriteStdin(gsl::span<const char> data);

    // Close the stdin pipe to signal EOF to the child process.
    void CloseStdin();

    // Stop worker threads and close all pipe handles, preparing the object for a
    // new redirection session. Safe to call even if no session was started before.
    void Reset();

  private:
    // Default buffer size for reading from stdout/stderr pipes. The buffer itself is
    // allocated on the heap by the worker thread, so it does not increase the size
    // of the thread stack frame.
    static constexpr size_t ReadBufferSize = 16384;

    // Platform-specific pipe handle type and invalid value.
#ifdef _WIN32
    using PipeHandle = void *; // HANDLE on Windows
    // INVALID_HANDLE_VALUE is (void*)-1
    static PipeHandle invalidPipe() noexcept
    {
        return reinterpret_cast<PipeHandle>(static_cast<std::intptr_t>(-1));
    }
#endif // _WIN32

#ifdef FEATURE_PAL
    using PipeHandle = int; // file descriptor on Unix
    static constexpr PipeHandle invalidPipe() noexcept
    {
        return -1;
    }
#endif // FEATURE_PAL

    // Output callback invoked when data arrives on stdout or stderr.
    OutputCallback m_callback;

    // Flag to signal worker threads to stop.
    std::atomic<bool> m_stopWorkers{false};

    // Mutex to protect the stdin pipe handles from concurrent access by
    // WriteStdin(), CloseStdin(), Exec(), and Reset().
    std::mutex m_stdinMutex;

    // Worker threads for reading stdout and stderr from the child process.
    std::thread m_stdoutThread;
    std::thread m_stderrThread;

    // Pipe endpoints for stdin (debugger writes, child reads).
    PipeHandle m_stdinRead{invalidPipe()};   // Child-side read end (given to child process).
    PipeHandle m_stdinWrite{invalidPipe()};  // Debugger-side write end.

    // Pipe endpoints for stdout (child writes, debugger reads).
    PipeHandle m_stdoutRead{invalidPipe()};  // Debugger-side read end.
    PipeHandle m_stdoutWrite{invalidPipe()}; // Child-side write end (given to child process).

    // Pipe endpoints for stderr (child writes, debugger reads).
    PipeHandle m_stderrRead{invalidPipe()};  // Debugger-side read end.
    PipeHandle m_stderrWrite{invalidPipe()}; // Child-side write end (given to child process).

#ifdef _WIN32
    // Manual-reset event signaled by Reset() to interrupt pending reads.
    PipeHandle m_stopEvent{invalidPipe()};
    // Manual-reset events associated with the overlapped reads of the worker threads.
    PipeHandle m_stdoutEvent{invalidPipe()};
    PipeHandle m_stderrEvent{invalidPipe()};
#endif // _WIN32

#ifdef FEATURE_PAL
    // Self-pipe used to interrupt poll() in the worker threads.
    PipeHandle m_wakeupRead{invalidPipe()};
    PipeHandle m_wakeupWrite{invalidPipe()};
#endif // FEATURE_PAL

    // Worker thread function that reads from a pipe and calls the output callback.
    void ReaderWorker(StreamType type);

    // Platform-specific helper methods (implemented in ioredirect_unix.cpp / ioredirect_win32.cpp).

    // Create an unnamed pipe. Returns true on success.
    // readEnd and writeEnd receive the two pipe endpoints. `overlapped` requests
    // asynchronous reads on readEnd (Windows only, ignored on POSIX).
    static bool CreatePipe(PipeHandle &readEnd, PipeHandle &writeEnd, bool overlapped);

    // Close a pipe handle. Sets the handle to invalidPipe().
    static void ClosePipe(PipeHandle &handle);

    // Write to a pipe. Returns number of bytes written, -1 on error.
    static int WritePipe(PipeHandle handle, const char *buffer, size_t size);

    // Set whether a pipe handle is inheritable by child processes.
    static bool SetInheritable(PipeHandle handle, bool inheritable);

    // Create the per-session primitive that interrupts blocking worker waits
    // (a self-pipe on POSIX, a manual-reset event on Windows). Returns true on success.
    [[nodiscard]] bool CreateWakeup();

    // Close the wakeup primitive and set it to invalidPipe().
    void CloseWakeup();

    // Wake both worker threads, so their pending waits fail.
    void SignalWakeup();

#ifdef _WIN32
    // Read from an overlapped pipe. Returns number of bytes read, 0 on EOF, -1 on
    // error or when the wakeup was signaled. `ioEvent` is the manual-reset event
    // associated with this read.
    int ReadPipe(PipeHandle handle, PipeHandle ioEvent, char *buffer, size_t size);
#endif // _WIN32

#ifdef FEATURE_PAL
    // Wait until the pipe is readable or the wakeup is signaled. Returns true if
    // data is available. Never blocks after Reset() has signaled the wakeup.
    bool WaitReadable(PipeHandle handle);

    // Read from a pipe. Returns number of bytes read, 0 on EOF, -1 on error.
    int ReadPipe(PipeHandle handle, char *buffer, size_t size);
#endif // FEATURE_PAL

    // Platform-specific saved state for standard file descriptor redirection.
    struct SavedStdFiles
    {
#ifdef _WIN32
        PipeHandle origStdin{invalidPipe()};
        PipeHandle origStdout{invalidPipe()};
        PipeHandle origStderr{invalidPipe()};
#endif // _WIN32

        int origStdinFd{-1};
        int origStdoutFd{-1};
        int origStderrFd{-1};

        bool valid{false};
    };

    // Redirect standard file descriptors to the given pipe handles.
    // Saves the original file descriptors for later restoration.
    static SavedStdFiles RedirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle, PipeHandle stderrHandle);

    // Restore standard file descriptors from saved state and release the saved
    // duplicates. Sets `valid` to false, so repeated calls do nothing.
    static void RestoreStdFiles(SavedStdFiles &saved);
};

} // namespace dncdbg

#endif // UTILS_IOREDIRECT_H
