// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace dncdbg
{

// Create an unnamed pipe. Returns true on success.
bool IORedirect::createPipe(PipeHandle &readEnd, PipeHandle &writeEnd)
{
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) < 0)
    {
        LOGE(log << "IORedirect::createPipe: pipe() failed, errno=" << errno);
        readEnd = invalidPipe();
        writeEnd = invalidPipe();
        return false;
    }

    // Ignore SIGPIPE to prevent crashes when writing to a broken pipe.
    static_cast<void>(signal(SIGPIPE, SIG_IGN));

    readEnd = fds.at(0);
    writeEnd = fds.at(1);
    return true;
}

// Close a pipe handle. Sets the handle to invalidPipe().
void IORedirect::closePipe(PipeHandle &handle)
{
    if (handle != invalidPipe())
    {
        ::close(handle);
        handle = invalidPipe();
    }
}

// Read from a pipe. Returns number of bytes read, 0 on EOF, -1 on error.
int IORedirect::readPipe(PipeHandle handle, char *buffer, size_t size)
{
    const ssize_t result = ::read(handle, buffer, size);
    if (result < 0)
    {
        return -1;
    }
    return static_cast<int>(result); // 0 means EOF
}

// Write to a pipe. Returns number of bytes written, -1 on error.
int IORedirect::writePipe(PipeHandle handle, const char *buffer, size_t size)
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
            LOGE(log << "IORedirect::writePipe: write() failed, errno=" << errno);
            return -1;
        }
        totalWritten += static_cast<size_t>(result);
    }

    return static_cast<int>(totalWritten);
}

// Set whether a pipe handle is inheritable by child processes.
bool IORedirect::setInheritable(PipeHandle handle, bool inheritable)
{
    int flags = fcntl(handle, F_GETFD); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (flags < 0)
    {
        LOGE(log << "IORedirect::setInheritable: fcntl(F_GETFD) failed, errno=" << errno);
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

    if (fcntl(handle, F_SETFD, flags) < 0) // NOLINT(cppcoreguidelines-pro-type-vararg)
    {
        LOGE(log << "IORedirect::setInheritable: fcntl(F_SETFD) failed, errno=" << errno);
        return false;
    }

    return true;
}

// Redirect standard file descriptors to the given pipe handles.
// Saves the original file descriptors for later restoration.
IORedirect::SavedStdFiles IORedirect::redirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle, PipeHandle stderrHandle)
{
    SavedStdFiles saved;

    // Save original file descriptors by duplicating them.
    saved.origStdinFd = ::dup(STDIN_FILENO);
    saved.origStdoutFd = ::dup(STDOUT_FILENO);
    saved.origStderrFd = ::dup(STDERR_FILENO);

    if (saved.origStdinFd == -1 || saved.origStdoutFd == -1 || saved.origStderrFd == -1)
    {
        LOGE(log << "IORedirect::redirectStdFiles: dup() failed, errno=" << errno);
        // Clean up any successful dups.
        if (saved.origStdinFd != -1)
        {
            ::close(saved.origStdinFd);
        }
        if (saved.origStdoutFd != -1)
        {
            ::close(saved.origStdoutFd);
        }
        if (saved.origStderrFd != -1)
        {
            ::close(saved.origStderrFd);
        }
        saved.valid = false;
        return saved;
    }

    // Redirect stdin/stdout/stderr to the pipe handles.
    if (::dup2(stdinHandle, STDIN_FILENO) == -1 ||
        ::dup2(stdoutHandle, STDOUT_FILENO) == -1 ||
        ::dup2(stderrHandle, STDERR_FILENO) == -1)
    {
        LOGE(log << "IORedirect::redirectStdFiles: dup2() failed, errno=" << errno);
        // Attempt to restore what we can.
        ::dup2(saved.origStdinFd, STDIN_FILENO);
        ::dup2(saved.origStdoutFd, STDOUT_FILENO);
        ::dup2(saved.origStderrFd, STDERR_FILENO);
        ::close(saved.origStdinFd);
        ::close(saved.origStdoutFd);
        ::close(saved.origStderrFd);
        saved.valid = false;
        return saved;
    }

    saved.valid = true;
    return saved;
}

// Restore standard file descriptors from saved state.
void IORedirect::restoreStdFiles(SavedStdFiles &saved)
{
    if (!saved.valid)
    {
        return;
    }

    // Restore original file descriptors.
    if (::dup2(saved.origStdinFd, STDIN_FILENO) == -1 ||
        ::dup2(saved.origStdoutFd, STDOUT_FILENO) == -1 ||
        ::dup2(saved.origStderrFd, STDERR_FILENO) == -1)
    {
        LOGE(log << "IORedirect::restoreStdFiles: dup2() failed, errno=" << errno);
    }

    // Close the saved duplicates.
    ::close(saved.origStdinFd);
    ::close(saved.origStdoutFd);
    ::close(saved.origStderrFd);

    saved.valid = false;
}

} // namespace dncdbg

#endif // FEATURE_PAL
