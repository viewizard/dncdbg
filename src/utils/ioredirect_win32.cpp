// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <limits>
#include <windows.h>

namespace dncdbg
{

namespace
{

// Size of the kernel buffer that holds the data written by the child process. A
// larger buffer keeps a chatty child process from blocking on write(), while the
// debugger-side worker thread drains it.
constexpr DWORD pipeBufferSize = 65536;

} // unnamed namespace

// Create an unnamed pipe. Returns true on success.
bool IORedirect::CreatePipe(PipeHandle &readEnd, PipeHandle &writeEnd, bool overlapped)
{
    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    if (!overlapped)
    {
        // Both ends are used synchronously: the child process reads its stdin and the
        // debugger writes to it, so an anonymous pipe is enough. The buffer size matches
        // the one used for the stdout and stderr pipes.
        if (!::CreatePipe(&readEnd, &writeEnd, &saAttr, pipeBufferSize))
        {
            LOGE(log << "IORedirect::CreatePipe: CreatePipe failed, error=" << GetLastError());
            readEnd = invalidPipe();
            writeEnd = invalidPipe();
            return false;
        }
        return true;
    }

    // Named pipes are used for the streams read by the debugger, since anonymous pipes
    // do not support overlapped I/O.
    static std::atomic<uint32_t> pipeNum{0};
    // The generated name is 33 characters long, so a small stack buffer is enough.
    std::array<char, 64> pipeName{};
    const int nameLength = snprintf(pipeName.data(), pipeName.size(), "\\\\.\\Pipe\\dncdbg.%08lx.%08lx",
                                    static_cast<unsigned long>(GetCurrentProcessId()),
                                    static_cast<unsigned long>(pipeNum.fetch_add(1)));
    if (nameLength <= 0 || nameLength >= static_cast<int>(pipeName.size()))
    {
        LOGE(log << "IORedirect::CreatePipe: failed to build the pipe name");
        readEnd = invalidPipe();
        writeEnd = invalidPipe();
        return false;
    }

    // The debugger only reads from this pipe, so its output buffer is not used.
    static constexpr DWORD outBufferSize = 0;

    // Create the reading end of the pipe. FILE_FLAG_OVERLAPPED makes ReadFile()
    // asynchronous, so the worker thread can be woken up by the stop event.
    readEnd = CreateNamedPipeA(pipeName.data(),
                               PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                               1, // Number of instances.
                               outBufferSize,
                               pipeBufferSize,
                               0, // Default timeout.
                               &saAttr);
    if (readEnd == invalidPipe())
    {
        LOGE(log << "IORedirect::CreatePipe: CreateNamedPipeA failed, error=" << GetLastError());
        return false;
    }

    // Create the writing end of the pipe. It belongs to the child process, so it is
    // opened in synchronous mode.
    writeEnd = CreateFileA(pipeName.data(),
                           GENERIC_WRITE,
                           0, // No sharing.
                           &saAttr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (writeEnd == invalidPipe())
    {
        LOGE(log << "IORedirect::CreatePipe: CreateFileA failed, error=" << GetLastError());
        CloseHandle(readEnd);
        readEnd = invalidPipe();
        return false;
    }

    return true;
}

// Close a pipe handle. Sets the handle to invalidPipe().
void IORedirect::ClosePipe(PipeHandle &handle)
{
    if (handle != invalidPipe())
    {
        CloseHandle(handle);
        handle = invalidPipe();
    }
}

// Create the events used to interrupt the worker threads.
bool IORedirect::CreateWakeup()
{
    // Manual-reset events are used: the stop event must stay signaled, and the
    // overlapped read events are reset explicitly before each read.
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent == nullptr)
    {
        LOGE(log << "IORedirect::CreateWakeup: CreateEvent(stop) failed, error=" << GetLastError());
        m_stopEvent = invalidPipe();
        return false;
    }

    m_stdoutEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stdoutEvent == nullptr)
    {
        LOGE(log << "IORedirect::CreateWakeup: CreateEvent(stdout) failed, error=" << GetLastError());
        m_stdoutEvent = invalidPipe();
        CloseWakeup();
        return false;
    }

    m_stderrEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stderrEvent == nullptr)
    {
        LOGE(log << "IORedirect::CreateWakeup: CreateEvent(stderr) failed, error=" << GetLastError());
        m_stderrEvent = invalidPipe();
        CloseWakeup();
        return false;
    }

    return true;
}

// Close the wakeup and overlapped I/O events.
void IORedirect::CloseWakeup()
{
    ClosePipe(m_stopEvent);
    ClosePipe(m_stdoutEvent);
    ClosePipe(m_stderrEvent);
}

// Wake both worker threads, so their pending waits return.
void IORedirect::SignalWakeup()
{
    if (m_stopEvent == invalidPipe())
    {
        return; // No redirection session is running.
    }

    if (!SetEvent(m_stopEvent))
    {
        LOGE(log << "IORedirect::SignalWakeup: SetEvent failed, error=" << GetLastError());
    }
}

// Read from an overlapped pipe. Returns number of bytes read, 0 on EOF, -1 on error
// or when the wakeup was signaled.
int IORedirect::ReadPipe(PipeHandle handle, PipeHandle ioEvent, char *buffer, size_t size)
{
    // A single overlapped request cannot transfer more than DWORD bytes.
    constexpr size_t maxTransfer = static_cast<size_t>(std::numeric_limits<DWORD>::max());
    const DWORD requested = static_cast<DWORD>(size < maxTransfer ? size : maxTransfer);

    // The event is reused by every read of this worker thread.
    if (!ResetEvent(ioEvent))
    {
        LOGE(log << "IORedirect::ReadPipe: ResetEvent failed, error=" << GetLastError());
        return -1;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = ioEvent;

    DWORD transferred = 0;
    if (!ReadFile(handle, buffer, requested, &transferred, &overlapped))
    {
        const DWORD error = GetLastError();

        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED)
        {
            return 0; // EOF: the child process closed its end of the pipe.
        }

        if (error != ERROR_IO_PENDING)
        {
            LOGE(log << "IORedirect::ReadPipe: ReadFile failed, error=" << error);
            return -1;
        }

        // Wait for the read to complete or for Reset() to signal the stop event.
        const std::array<PipeHandle, 2> events{ioEvent, m_stopEvent};
        const DWORD waitResult = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(),
                                                        FALSE, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
        {
            // The stop was requested (or the wait failed): cancel the read and wait
            // until it is really gone, since the OVERLAPPED must not outlive this call.
            static_cast<void>(CancelIoEx(handle, &overlapped));
            static_cast<void>(GetOverlappedResult(handle, &overlapped, &transferred, TRUE));
            return -1;
        }

        transferred = 0;
        if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
        {
            const DWORD resultError = GetLastError();
            if (resultError == ERROR_BROKEN_PIPE || resultError == ERROR_NO_DATA ||
                resultError == ERROR_PIPE_NOT_CONNECTED)
            {
                return 0; // EOF.
            }
            if (resultError != ERROR_OPERATION_ABORTED)
            {
                LOGE(log << "IORedirect::ReadPipe: GetOverlappedResult failed, error=" << resultError);
            }
            return -1;
        }

        return static_cast<int>(transferred);
    }

    // The read completed synchronously. The byte count is still reported through
    // GetOverlappedResult(), which is the documented way to read it for overlapped I/O.
    transferred = 0;
    if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
    {
        LOGE(log << "IORedirect::ReadPipe: GetOverlappedResult failed, error=" << GetLastError());
        return -1;
    }

    return static_cast<int>(transferred);
}

// Write to a pipe. Returns number of bytes written, -1 on error.
int IORedirect::WritePipe(PipeHandle handle, const char *buffer, size_t size)
{
    constexpr size_t maxTransfer = static_cast<size_t>(std::numeric_limits<DWORD>::max());
    const DWORD total = static_cast<DWORD>(size < maxTransfer ? size : maxTransfer);

    DWORD totalWritten = 0;

    // Write all data, handling partial writes.
    while (totalWritten < total)
    {
        DWORD bytesWritten = 0;
        if (!WriteFile(handle, buffer + totalWritten, total - totalWritten, &bytesWritten, nullptr))
        {
            LOGE(log << "IORedirect::WritePipe: WriteFile failed, error=" << GetLastError());
            return -1;
        }
        if (bytesWritten == 0)
        {
            break; // Cannot make progress, do not spin.
        }
        totalWritten += bytesWritten;
    }

    // Clamp instead of overflowing the int result.
    constexpr DWORD maxResult = static_cast<DWORD>(std::numeric_limits<int>::max());
    return static_cast<int>(totalWritten < maxResult ? totalWritten : maxResult);
}

// Set whether a pipe handle is inheritable by child processes.
bool IORedirect::SetInheritable(PipeHandle handle, bool inheritable)
{
    const DWORD flags = inheritable ? HANDLE_FLAG_INHERIT : 0;
    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, flags))
    {
        LOGE(log << "IORedirect::SetInheritable: SetHandleInformation failed, error=" << GetLastError());
        return false;
    }
    return true;
}

// Redirect standard file descriptors to the given pipe handles.
// Saves the original handles and C runtime file descriptors for later restoration.
IORedirect::SavedStdFiles IORedirect::RedirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle,
                                                       PipeHandle stderrHandle)
{
    SavedStdFiles saved;

    // Save original Win32 standard handles.
    saved.origStdin = GetStdHandle(STD_INPUT_HANDLE);
    saved.origStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    saved.origStderr = GetStdHandle(STD_ERROR_HANDLE);

    if (saved.origStdin == invalidPipe() || saved.origStdout == invalidPipe() || saved.origStderr == invalidPipe())
    {
        LOGE(log << "IORedirect::RedirectStdFiles: GetStdHandle failed");
        return saved;
    }

    // Flush the C runtime buffers, so buffered debugger output is written to the
    // original standard handles and not into the pipes.
    fflush(stdout);
    fflush(stderr);

    // From this point on the standard handles must be restored, even if the
    // redirection below fails halfway.
    saved.valid = true;

    // Set new Win32 standard handles.
    if (!SetStdHandle(STD_INPUT_HANDLE, stdinHandle) ||
        !SetStdHandle(STD_OUTPUT_HANDLE, stdoutHandle) ||
        !SetStdHandle(STD_ERROR_HANDLE, stderrHandle))
    {
        LOGE(log << "IORedirect::RedirectStdFiles: SetStdHandle failed, error=" << GetLastError());
        RestoreStdFiles(saved);
        return saved;
    }

    // Duplicate the pipe handles for the C runtime. _open_osfhandle() takes ownership
    // of the handle it receives, while the pipe handles must stay owned by IORedirect.
    // The duplicates are not inheritable: the child process gets the pipe ends only
    // through its standard handles.
    HANDLE stdinDup = nullptr;
    HANDLE stdoutDup = nullptr;
    HANDLE stderrDup = nullptr;
    const HANDLE currentProcess = GetCurrentProcess();
    if (!DuplicateHandle(currentProcess, stdinHandle, currentProcess, &stdinDup, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(currentProcess, stdoutHandle, currentProcess, &stdoutDup, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(currentProcess, stderrHandle, currentProcess, &stderrDup, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        LOGE(log << "IORedirect::RedirectStdFiles: DuplicateHandle failed, error=" << GetLastError());
        if (stdinDup != nullptr)
        {
            CloseHandle(stdinDup);
        }
        if (stdoutDup != nullptr)
        {
            CloseHandle(stdoutDup);
        }
        if (stderrDup != nullptr)
        {
            CloseHandle(stderrDup);
        }
        RestoreStdFiles(saved);
        return saved;
    }

    // Save original C runtime file descriptors. A negative descriptor means that the
    // corresponding stream is not open at all; the CRT must not be asked to handle
    // such a value.
    const int stdinFd = _fileno(stdin);
    const int stdoutFd = _fileno(stdout);
    const int stderrFd = _fileno(stderr);

    if (stdinFd >= 0)
    {
        saved.origStdinFd = _dup(stdinFd);
    }
    if (stdoutFd >= 0)
    {
        saved.origStdoutFd = _dup(stdoutFd);
    }
    if (stderrFd >= 0)
    {
        saved.origStderrFd = _dup(stderrFd);
    }

    // Redirect the C runtime file descriptors, so C stdio functions in this process
    // use the pipes as well while the child process is being created.
    static constexpr int openFlagsIn = _O_RDONLY | _O_BINARY;
    static constexpr int openFlagsOut = _O_BINARY;

    if (stdinFd >= 0)
    {
        const int newStdinFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdinDup), openFlagsIn);
        if (newStdinFd == -1)
        {
            LOGE(log << "IORedirect::RedirectStdFiles: _open_osfhandle(stdin) failed, errno=" << errno);
            CloseHandle(stdinDup);
        }
        else
        {
            if (_dup2(newStdinFd, stdinFd) == -1)
            {
                LOGE(log << "IORedirect::RedirectStdFiles: _dup2(stdin) failed, errno=" << errno);
            }
            _close(newStdinFd); // The duplicate is referenced by stdinFd now.
        }
    }
    else
    {
        CloseHandle(stdinDup);
    }

    if (stdoutFd >= 0)
    {
        const int newStdoutFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdoutDup), openFlagsOut);
        if (newStdoutFd == -1)
        {
            LOGE(log << "IORedirect::RedirectStdFiles: _open_osfhandle(stdout) failed, errno=" << errno);
            CloseHandle(stdoutDup);
        }
        else
        {
            if (_dup2(newStdoutFd, stdoutFd) == -1)
            {
                LOGE(log << "IORedirect::RedirectStdFiles: _dup2(stdout) failed, errno=" << errno);
            }
            _close(newStdoutFd); // The duplicate is referenced by stdoutFd now.
        }
    }
    else
    {
        CloseHandle(stdoutDup);
    }

    if (stderrFd >= 0)
    {
        const int newStderrFd = _open_osfhandle(reinterpret_cast<intptr_t>(stderrDup), openFlagsOut);
        if (newStderrFd == -1)
        {
            LOGE(log << "IORedirect::RedirectStdFiles: _open_osfhandle(stderr) failed, errno=" << errno);
            CloseHandle(stderrDup);
        }
        else
        {
            if (_dup2(newStderrFd, stderrFd) == -1)
            {
                LOGE(log << "IORedirect::RedirectStdFiles: _dup2(stderr) failed, errno=" << errno);
            }
            _close(newStderrFd); // The duplicate is referenced by stderrFd now.
        }
    }
    else
    {
        CloseHandle(stderrDup);
    }

    return saved;
}

// Restore standard file descriptors from saved state.
void IORedirect::RestoreStdFiles(SavedStdFiles &saved)
{
    if (!saved.valid)
    {
        return;
    }

    saved.valid = false;

    // Flush the C runtime buffers before the handles are replaced again.
    fflush(stdout);
    fflush(stderr);

    // Restore Win32 standard handles.
    SetStdHandle(STD_INPUT_HANDLE, saved.origStdin);
    SetStdHandle(STD_OUTPUT_HANDLE, saved.origStdout);
    SetStdHandle(STD_ERROR_HANDLE, saved.origStderr);

    // Restore the C runtime file descriptors. _dup2() closes the descriptors that
    // stdin, stdout and stderr currently use, which releases the pipe duplicates.
    // The saved copies are closed once they have been duplicated back.
    const int stdinFd = _fileno(stdin);
    const int stdoutFd = _fileno(stdout);
    const int stderrFd = _fileno(stderr);

    if (stdinFd >= 0 && saved.origStdinFd != -1)
    {
        if (_dup2(saved.origStdinFd, stdinFd) == -1)
        {
            LOGE(log << "IORedirect::RestoreStdFiles: _dup2(stdin) failed, errno=" << errno);
        }
        _close(saved.origStdinFd);
        saved.origStdinFd = -1;
    }
    if (stdoutFd >= 0 && saved.origStdoutFd != -1)
    {
        if (_dup2(saved.origStdoutFd, stdoutFd) == -1)
        {
            LOGE(log << "IORedirect::RestoreStdFiles: _dup2(stdout) failed, errno=" << errno);
        }
        _close(saved.origStdoutFd);
        saved.origStdoutFd = -1;
    }
    if (stderrFd >= 0 && saved.origStderrFd != -1)
    {
        if (_dup2(saved.origStderrFd, stderrFd) == -1)
        {
            LOGE(log << "IORedirect::RestoreStdFiles: _dup2(stderr) failed, errno=" << errno);
        }
        _close(saved.origStderrFd);
        saved.origStderrFd = -1;
    }
}

} // namespace dncdbg

#endif // _WIN32
