// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

namespace dncdbg
{

// Create an unnamed pipe using named pipes with overlapped IO support.
// Returns true on success.
bool IORedirect::createPipe(PipeHandle &readEnd, PipeHandle &writeEnd)
{
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    // Use named pipes to support overlapped IO on Windows.
    static std::atomic<long> pipeNum;
    char pipeName[MAX_PATH + 1];
    snprintf(pipeName, sizeof(pipeName), "\\\\.\\Pipe\\dncdbg.%08x.%08lx",
             GetCurrentProcessId(), pipeNum++);

    static constexpr DWORD PipeSize = 65536;

    // Create the reading end of the pipe.
    readEnd = CreateNamedPipeA(
        pipeName,
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,         // Number of instances.
        PipeSize,  // Output buffer size.
        PipeSize,  // Input buffer size.
        0,         // Default timeout.
        &saAttr);

    if (readEnd == invalidPipe())
    {
        LOGE(log << "IORedirect::createPipe: CreateNamedPipeA failed, error=" << GetLastError());
        return false;
    }

    // Create the writing end of the pipe.
    writeEnd = CreateFileA(
        pipeName,
        GENERIC_WRITE,
        0,         // No sharing.
        &saAttr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (writeEnd == invalidPipe())
    {
        LOGE(log << "IORedirect::createPipe: CreateFileA failed, error=" << GetLastError());
        CloseHandle(readEnd);
        readEnd = invalidPipe();
        return false;
    }

    return true;
}

// Close a pipe handle. Sets the handle to invalidPipe().
void IORedirect::closePipe(PipeHandle &handle)
{
    if (handle != invalidPipe())
    {
        CloseHandle(handle);
        handle = invalidPipe();
    }
}

// Read from a pipe. Returns number of bytes read, 0 on EOF, -1 on error.
int IORedirect::readPipe(PipeHandle handle, char *buffer, size_t size)
{
    DWORD bytesRead = 0;
    if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &bytesRead, nullptr))
    {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA)
        {
            return 0; // EOF: writing end of pipe was closed.
        }
        return -1; // Error.
    }

    if (bytesRead == 0)
    {
        return 0; // EOF.
    }

    return static_cast<int>(bytesRead);
}

// Write to a pipe. Returns number of bytes written, -1 on error.
int IORedirect::writePipe(PipeHandle handle, const char *buffer, size_t size)
{
    DWORD totalWritten = 0;

    // Write all data, handling partial writes.
    while (totalWritten < static_cast<DWORD>(size))
    {
        DWORD bytesWritten = 0;
        if (!WriteFile(handle, buffer + totalWritten,
                       static_cast<DWORD>(size) - totalWritten, &bytesWritten, nullptr))
        {
            LOGE(log << "IORedirect::writePipe: WriteFile failed, error=" << GetLastError());
            return -1;
        }
        totalWritten += bytesWritten;
    }

    return static_cast<int>(totalWritten);
}

// Set whether a pipe handle is inheritable by child processes.
bool IORedirect::setInheritable(PipeHandle handle, bool inheritable)
{
    const DWORD flags = inheritable ? HANDLE_FLAG_INHERIT : 0;
    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, flags))
    {
        LOGE(log << "IORedirect::setInheritable: SetHandleInformation failed, error=" << GetLastError());
        return false;
    }
    return true;
}

// Redirect standard file descriptors to the given pipe handles.
// Saves the original handles and C runtime file descriptors for later restoration.
IORedirect::SavedStdFiles IORedirect::redirectStdFiles(PipeHandle stdinHandle, PipeHandle stdoutHandle, PipeHandle stderrHandle)
{
    SavedStdFiles saved;

    // Save original Win32 standard handles.
    saved.origStdin = GetStdHandle(STD_INPUT_HANDLE);
    saved.origStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    saved.origStderr = GetStdHandle(STD_ERROR_HANDLE);

    if (saved.origStdin == invalidPipe() || saved.origStdout == invalidPipe() || saved.origStderr == invalidPipe())
    {
        LOGE(log << "IORedirect::redirectStdFiles: GetStdHandle failed");
        saved.valid = false;
        return saved;
    }

    // Flush C runtime buffers before redirecting.
    fflush(stdout);
    fflush(stderr);

    // Make child-side pipe ends inheritable for the child process.
    setInheritable(stdinHandle, true);
    setInheritable(stdoutHandle, true);
    setInheritable(stderrHandle, true);

    // Set new Win32 standard handles.
    if (!SetStdHandle(STD_INPUT_HANDLE, stdinHandle) ||
        !SetStdHandle(STD_OUTPUT_HANDLE, stdoutHandle) ||
        !SetStdHandle(STD_ERROR_HANDLE, stderrHandle))
    {
        LOGE(log << "IORedirect::redirectStdFiles: SetStdHandle failed, error=" << GetLastError());
        // Restore original handles on failure.
        SetStdHandle(STD_INPUT_HANDLE, saved.origStdin);
        SetStdHandle(STD_OUTPUT_HANDLE, saved.origStdout);
        SetStdHandle(STD_ERROR_HANDLE, saved.origStderr);
        saved.valid = false;
        return saved;
    }

    // Also redirect C runtime file descriptors so that C stdio functions work correctly.
    static constexpr int openFlagsIn = _O_RDONLY | _O_BINARY;
    static constexpr int openFlagsOut = _O_BINARY;

    const int stdinFd = _fileno(stdin);
    const int stdoutFd = _fileno(stdout);
    const int stderrFd = _fileno(stderr);

    // Save original C runtime file descriptors.
    saved.origStdinFd = _dup(stdinFd);
    saved.origStdoutFd = _dup(stdoutFd);
    saved.origStderrFd = _dup(stderrFd);

    // Create new C runtime file descriptors from the pipe handles.
    const int newStdinFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdinHandle), openFlagsIn);
    const int newStdoutFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdoutHandle), openFlagsOut);
    const int newStderrFd = _open_osfhandle(reinterpret_cast<intptr_t>(stderrHandle), openFlagsOut);

    if (newStdinFd != -1)
    {
        _dup2(newStdinFd, stdinFd);
    }
    if (newStdoutFd != -1)
    {
        _dup2(newStdoutFd, stdoutFd);
    }
    if (newStderrFd != -1)
    {
        _dup2(newStderrFd, stderrFd);
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

    // Flush C runtime buffers before restoring.
    fflush(stdout);
    fflush(stderr);

    // Restore Win32 standard handles.
    SetStdHandle(STD_INPUT_HANDLE, saved.origStdin);
    SetStdHandle(STD_OUTPUT_HANDLE, saved.origStdout);
    SetStdHandle(STD_ERROR_HANDLE, saved.origStderr);

    // Restore C runtime file descriptors.
    const int stdinFd = _fileno(stdin);
    const int stdoutFd = _fileno(stdout);
    const int stderrFd = _fileno(stderr);

    if (saved.origStdinFd != -1)
    {
        _dup2(saved.origStdinFd, stdinFd);
        _close(saved.origStdinFd);
    }
    if (saved.origStdoutFd != -1)
    {
        _dup2(saved.origStdoutFd, stdoutFd);
        _close(saved.origStdoutFd);
    }
    if (saved.origStderrFd != -1)
    {
        _dup2(saved.origStderrFd, stderrFd);
        _close(saved.origStderrFd);
    }

    saved.valid = false;
}

} // namespace dncdbg

#endif // _WIN32
