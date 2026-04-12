// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_IOSYSTEM_H
#define UTILS_IOSYSTEM_H

#include <gsl/span>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace dncdbg
{

// IORedirect class provides IO redirection for a child process.
//
// It creates unnamed pipes for stdin, stdout, and stderr, then allows launching
// a child process with those pipes substituted for the standard file descriptors.
// After the child process is launched, worker threads read from the child's stdout
// and stderr pipes and deliver data via a callback. The debugger can also write
// data to the child's stdin pipe.
//
// Usage:
//   1. Construct IORedirect with an OutputCallback.
//   2. Call exec() with a lambda that creates the child process.
//      Inside the lambda, stdin/stdout/stderr are redirected to the pipes.
//   3. The OutputCallback is called from worker threads when the child writes
//      to stdout or stderr.
//   4. Call writeStdin() to send data to the child's stdin.
//   5. Destruction stops worker threads and closes all pipes.
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
    // writes to stdout or stderr. Creates all necessary pipes internally.
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
    // This method can only be called once.
    void exec(const std::function<void()> &func);

    // Write data to the child process's stdin pipe.
    //
    // The data from `data` is written to the stdin pipe.
    // Returns the number of bytes actually written, or -1 on error.
    // Returns 0 if the stdin pipe has been closed.
    int writeStdin(gsl::span<const char> data);

    // Close the stdin pipe to signal EOF to the child process.
    void closeStdin();

  private:

    // Default buffer size for reading from stdout/stderr pipes.
    static constexpr size_t ReadBufferSize = 4096;

    // Output callback invoked when data arrives on stdout or stderr.
    OutputCallback m_callback;

    // Flag to track whether exec() has been called.
    bool m_execCalled{false};

    // Flag to signal worker threads to stop.
    std::atomic<bool> m_stopWorkers{false};

    // Mutex to protect writeStdin() and closeStdin() from concurrent access.
    std::mutex m_stdinMutex;

    // Worker threads for reading stdout and stderr from the child process.
    std::thread m_stdoutThread;
    std::thread m_stderrThread;

    // Worker thread function that reads from a pipe and calls the output callback.
    void readerWorker(StreamType type);

    // Platform-specific pipe handle type and invalid value.
#ifdef _WIN32
    using PipeHandle = void *; // HANDLE on Windows
    // INVALID_HANDLE_VALUE is (void*)-1
    static PipeHandle invalidPipe()
    {
        return reinterpret_cast<PipeHandle>(static_cast<intptr_t>(-1));
    }
#endif // _WIN32

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    using PipeHandle = int; // file descriptor on Unix
    static PipeHandle invalidPipe()
    {
        return -1;
    }
#endif // __unix__

    // Pipe endpoints for stdin (debugger writes, child reads).
    PipeHandle m_stdinRead;   // Child-side read end (given to child process).
    PipeHandle m_stdinWrite;  // Debugger-side write end.

    // Pipe endpoints for stdout (child writes, debugger reads).
    PipeHandle m_stdoutRead;  // Debugger-side read end.
    PipeHandle m_stdoutWrite; // Child-side write end (given to child process).

    // Pipe endpoints for stderr (child writes, debugger reads).
    PipeHandle m_stderrRead;  // Debugger-side read end.
    PipeHandle m_stderrWrite; // Child-side write end (given to child process).

    // Platform-specific helper methods (implemented in iosystem_unix.cpp / iosystem_win32.cpp).

    // Create an unnamed pipe. Returns true on success.
    // readEnd and writeEnd receive the two pipe endpoints.
    static bool createPipe(PipeHandle &readEnd, PipeHandle &writeEnd);

    // Close a pipe handle. Sets the handle to invalidPipe().
    static void closePipe(PipeHandle &handle);

    // Read from a pipe. Returns number of bytes read, 0 on EOF, -1 on error.
    static int readPipe(PipeHandle handle, char *buffer, size_t size);

    // Write to a pipe. Returns number of bytes written, -1 on error.
    static int writePipe(PipeHandle handle, const char *buffer, size_t size);

    // Set whether a pipe handle is inheritable by child processes.
    static bool setInheritable(PipeHandle handle, bool inheritable);

    // Platform-specific saved state for standard file descriptor redirection.
    struct SavedStdFiles
    {
#ifdef _WIN32
        PipeHandle origStdin{invalidPipe()};
        PipeHandle origStdout{invalidPipe()};
        PipeHandle origStderr{invalidPipe()};
        int origStdinFd{-1};
        int origStdoutFd{-1};
        int origStderrFd{-1};
#endif // _WIN32

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
        int origStdinFd{-1};
        int origStdoutFd{-1};
        int origStderrFd{-1};
#endif // __unix__

        bool valid{false};
    };

    // Redirect standard file descriptors to the given pipe handles.
    // Saves the original file descriptors for later restoration.
    static SavedStdFiles redirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle, PipeHandle stderrHandle);

    // Restore standard file descriptors from saved state.
    static void restoreStdFiles(SavedStdFiles &saved);
};

} // namespace dncdbg

#endif // UTILS_IOSYSTEM_H
