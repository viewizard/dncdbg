// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <poll.h>
#include <unistd.h>

namespace dncdbg
{

// Create an unnamed pipe. Returns true on success.
// POSIX pipes are always blocking, so the `overlapped` request is not used here:
// WaitReadable() makes the wait for data interruptible instead.
bool IORedirect::CreatePipe(PipeHandle &readEnd, PipeHandle &writeEnd, bool /*overlapped*/)
{
    // A write to a pipe whose read end is already closed raises SIGPIPE, which by
    // default terminates the debugger. Ignore it once for the whole process.
    static std::once_flag sigpipeOnce;
    std::call_once(sigpipeOnce, []
    {
        static_cast<void>(::signal(SIGPIPE, SIG_IGN));
    });

    std::array<int, 2> fds{};
#if defined(__linux__)
    // Both ends are created close-on-exec atomically, so a concurrent fork()+exec() in
    // another thread cannot leak the debugger-side ends into a child process.
    const int pipeResult = ::pipe2(fds.data(), O_CLOEXEC);
#else
    const int pipeResult = ::pipe(fds.data());
#endif
    if (pipeResult < 0)
    {
        LOGE(log << "IORedirect::CreatePipe: pipe() failed, errno=" << errno);
        readEnd = invalidPipe();
        writeEnd = invalidPipe();
        return false;
    }

    readEnd = fds.at(0);
    writeEnd = fds.at(1);
    return true;
}

// Close a pipe handle. Sets the handle to invalidPipe().
void IORedirect::ClosePipe(PipeHandle &handle)
{
    if (handle != invalidPipe())
    {
        ::close(handle);
        handle = invalidPipe();
    }
}

// Read from a pipe. Returns number of bytes read, 0 on EOF, -1 on error.
int IORedirect::ReadPipe(PipeHandle handle, char *buffer, size_t size)
{
    ssize_t result = 0;

    // Retry on interrupts, unless the session is being stopped.
    do
    {
        result = ::read(handle, buffer, size);
    }
    while (result < 0 && errno == EINTR && !m_stopWorkers.load());

    if (result < 0)
    {
        // A failed read is expected when the session is being stopped.
        if (!m_stopWorkers.load())
        {
            LOGE(log << "IORedirect::ReadPipe: read() failed, errno=" << errno);
        }
        return -1;
    }

    // The request size is ReadBufferSize, so the result always fits into int.
    return static_cast<int>(result); // 0 means EOF
}

// Write to a pipe. Returns number of bytes written, -1 on error.
int IORedirect::WritePipe(PipeHandle handle, const char *buffer, size_t size)
{
    size_t totalWritten = 0;

    // Write all data, handling partial writes.
    while (totalWritten < size)
    {
        const ssize_t result = ::write(handle, buffer + totalWritten, size - totalWritten);
        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue; // Retry on interrupt.
            }
            LOGE(log << "IORedirect::WritePipe: write() failed, errno=" << errno);
            return -1;
        }
        if (result == 0)
        {
            break; // Cannot make progress, do not spin.
        }
        totalWritten += static_cast<size_t>(result);
    }

    // Clamp instead of overflowing the int result.
    constexpr auto maxResult = static_cast<size_t>(std::numeric_limits<int>::max());
    return static_cast<int>(totalWritten < maxResult ? totalWritten : maxResult);
}

// Set whether a pipe handle is inheritable by child processes.
bool IORedirect::SetInheritable(PipeHandle handle, bool inheritable)
{
    int flags = ::fcntl(handle, F_GETFD); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (flags < 0)
    {
        LOGE(log << "IORedirect::SetInheritable: fcntl(F_GETFD) failed, errno=" << errno);
        return false;
    }

    if (inheritable)
    {
        flags &= ~FD_CLOEXEC; // Allow inheritance.
    }
    else
    {
        flags |= FD_CLOEXEC; // Prevent inheritance.
    }

    if (::fcntl(handle, F_SETFD, flags) < 0) // NOLINT(cppcoreguidelines-pro-type-vararg)
    {
        LOGE(log << "IORedirect::SetInheritable: fcntl(F_SETFD) failed, errno=" << errno);
        return false;
    }

    return true;
}

// Create the self-pipe used to interrupt poll() in the worker threads.
bool IORedirect::CreateWakeup()
{
    std::array<int, 2> fds{};
#if defined(__linux__)
    // Created close-on-exec atomically, so the self-pipe cannot leak into a concurrent fork().
    const int pipeResult = ::pipe2(fds.data(), O_CLOEXEC);
#else
    const int pipeResult = ::pipe(fds.data());
#endif
    if (pipeResult < 0)
    {
        LOGE(log << "IORedirect::CreateWakeup: pipe() failed, errno=" << errno);
        return false;
    }

    m_wakeupRead = fds.at(0);
    m_wakeupWrite = fds.at(1);

#if !defined(__linux__)
    // The self-pipe is private to this process and must not leak into the child.
    if (!SetInheritable(m_wakeupRead, false) || !SetInheritable(m_wakeupWrite, false))
    {
        CloseWakeup();
        return false;
    }
#endif

    return true;
}

// Close the self-pipe.
void IORedirect::CloseWakeup()
{
    ClosePipe(m_wakeupRead);
    ClosePipe(m_wakeupWrite);
}

// Wake both worker threads, so their pending poll() calls return.
void IORedirect::SignalWakeup()
{
    if (m_wakeupWrite == invalidPipe())
    {
        return; // No redirection session is running.
    }

    // poll() is level-triggered, so a single byte keeps the self-pipe readable for
    // all waiters. The byte is dropped together with the self-pipe in Reset().
    constexpr char wakeupByte = 1;
    ssize_t result = 0;
    do
    {
        result = ::write(m_wakeupWrite, &wakeupByte, sizeof(wakeupByte));
    }
    while (result < 0 && errno == EINTR);

    if (result < 0)
    {
        // The stop flag is set as well, but a failed wakeup can leave a worker blocked
        // in poll() until data or EOF arrives.
        LOGE(log << "IORedirect::SignalWakeup: write() failed, errno=" << errno);
    }
}

// Wait until the pipe is readable or the wakeup is signaled.
bool IORedirect::WaitReadable(PipeHandle handle)
{
    std::array<pollfd, 2> fds{};
    fds.at(0).fd = handle;
    fds.at(0).events = POLLIN;
    fds.at(1).fd = m_wakeupRead;
    fds.at(1).events = POLLIN;

    const auto count = static_cast<nfds_t>(fds.size());
    constexpr int noTimeout = -1; // Block until data arrives or the wakeup is signaled.

    while (!m_stopWorkers.load())
    {
        const int result = ::poll(fds.data(), count, noTimeout);
        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            LOGE(log << "IORedirect::WaitReadable: poll() failed, errno=" << errno);
            return false;
        }

        if (fds.at(1).revents != 0)
        {
            return false; // Reset() signaled the stop.
        }

        if (fds.at(0).revents != 0)
        {
            // Data, EOF (POLLHUP), or a pipe error: read() reports which one it is.
            return true;
        }
    }

    return false;
}

// Redirect standard file descriptors to the given pipe handles.
// Saves the original file descriptors for later restoration.
IORedirect::SavedStdFiles IORedirect::RedirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle,
                                                       PipeHandle stderrHandle)
{
    SavedStdFiles saved;

    // Save original file descriptors by duplicating them.
    // F_DUPFD_CLOEXEC keeps the saved copies out of any child forked concurrently.
    saved.origStdinFd = ::fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0); // NOLINT(cppcoreguidelines-pro-type-vararg)
    saved.origStdoutFd = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0); // NOLINT(cppcoreguidelines-pro-type-vararg)
    saved.origStderrFd = ::fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0); // NOLINT(cppcoreguidelines-pro-type-vararg)

    if (saved.origStdinFd == -1 || saved.origStdoutFd == -1 || saved.origStderrFd == -1)
    {
        LOGE(log << "IORedirect::RedirectStdFiles: dup() failed, errno=" << errno);
        RestoreStdFiles(saved); // Release whatever was duplicated.
        return saved;
    }

    // From this point on the standard descriptors must be restored, even if the
    // redirection below fails halfway.
    saved.valid = true;

    // Redirect stdin/stdout/stderr to the pipe handles.
    if (::dup2(stdinHandle, STDIN_FILENO) == -1 ||
        ::dup2(stdoutHandle, STDOUT_FILENO) == -1 ||
        ::dup2(stderrHandle, STDERR_FILENO) == -1)
    {
        LOGE(log << "IORedirect::RedirectStdFiles: dup2() failed, errno=" << errno);
        RestoreStdFiles(saved); // Put the original descriptors back.
        return saved;
    }

    return saved;
}

// Restore standard file descriptors from saved state and release the saved duplicates.
void IORedirect::RestoreStdFiles(SavedStdFiles &saved)
{
    if (saved.valid)
    {
        saved.valid = false;

        // Restore original file descriptors.
        if (::dup2(saved.origStdinFd, STDIN_FILENO) == -1 ||
            ::dup2(saved.origStdoutFd, STDOUT_FILENO) == -1 ||
            ::dup2(saved.origStderrFd, STDERR_FILENO) == -1)
        {
            LOGE(log << "IORedirect::RestoreStdFiles: dup2() failed, errno=" << errno);
        }
    }

    // Close the saved duplicates. They exist even if the redirection failed halfway.
    if (saved.origStdinFd != -1)
    {
        ::close(saved.origStdinFd);
        saved.origStdinFd = -1;
    }
    if (saved.origStdoutFd != -1)
    {
        ::close(saved.origStdoutFd);
        saved.origStdoutFd = -1;
    }
    if (saved.origStderrFd != -1)
    {
        ::close(saved.origStderrFd);
        saved.origStderrFd = -1;
    }
}

} // namespace dncdbg

#endif // FEATURE_PAL
