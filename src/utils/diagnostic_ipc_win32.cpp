// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/diagnostic_ipc.h"
#include "utils/int_to_string.h"
#include "utils/logger.h"
#include <windows.h>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace dncdbg::DiagnosticIpc
{

namespace
{

// The runtime creates its default diagnostic endpoint as a named pipe with a
// deterministic name "\\.\pipe\dotnet-diagnostic-{PID}", so no scanning is
// needed on Windows.
constexpr std::string_view kPipePrefix = "\\\\.\\pipe\\dotnet-diagnostic-";

// Maximum number of decimal digits of an unsigned 32-bit PID (no sign involved).
constexpr size_t kPidMaxDigits = static_cast<size_t>(std::numeric_limits<uint32_t>::digits10) + 1;

// Largest number of bytes passed to a single ReadFile() / WriteFile() call, which
// keeps each call well inside the 32-bit DWORD range.
constexpr size_t kMaxIoChunk = 0x40000000; // 1 GiB

} // unnamed namespace

// Endpoint discovery: format the deterministic pipe name for `pid`.
// Always succeeds for a syntactically valid PID.
HRESULT IpcEndpointResolve(uint32_t pid, std::string &outEndpoint)
{
    outEndpoint.clear();

    outEndpoint.reserve(kPipePrefix.size() + kPidMaxDigits);
    outEndpoint.append(kPipePrefix);
    AppendInteger(outEndpoint, pid);
    return S_OK;
}

// Connects to the byte-mode named pipe `endpoint` (the server side is created
// with PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, a synchronous client handle
// is valid against such an instance). Behavior mirrors the C# NamedPipeClientStream:
// - ERROR_FILE_NOT_FOUND is returned immediately, without retry;
// - ERROR_PIPE_BUSY drives a WaitNamedPipeA wait and a CreateFileA re-attempt,
//   both sharing a single kConnectTimeoutMs deadline.
HRESULT IpcStreamOpen(const std::string &endpoint, IpcHandle &outHandle)
{
    outHandle = kInvalidIpcHandle;

    const ULONGLONG deadline = GetTickCount64() + kConnectTimeoutMs;

    while (true)
    {
        HANDLE handle = CreateFileA(endpoint.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, // no sharing
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (handle != INVALID_HANDLE_VALUE)
        {
            outHandle = handle;
            return S_OK;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
        {
            LOGD(log << "DiagnosticIpc: pipe does not exist: " << endpoint);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        if (error != ERROR_PIPE_BUSY)
        {
            LOGE(log << "DiagnosticIpc: CreateFileA failed for '" << endpoint << "', error=" << error);
            return E_FAIL;
        }

        // All pipe instances are busy: wait until one becomes available or the
        // overall deadline elapses, then re-attempt CreateFileA.
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            LOGE(log << "DiagnosticIpc: connect timed out for '" << endpoint << "'");
            return HRESULT_FROM_WIN32(ERROR_PIPE_BUSY);
        }

        const DWORD remaining = static_cast<DWORD>(deadline - now);
        if (!WaitNamedPipeA(endpoint.c_str(), remaining))
        {
            LOGE(log << "DiagnosticIpc: WaitNamedPipeA failed for '" << endpoint << "', error=" << GetLastError());
            return HRESULT_FROM_WIN32(ERROR_PIPE_BUSY);
        }
    }
}

// Closes the handle; nullptr is normalized to a no-op.
void IpcStreamClose(IpcHandle handle)
{
    if (handle != kInvalidIpcHandle)
    {
        static_cast<void>(CloseHandle(handle));
    }
}

// ReadFile loop until exactly `length` bytes are received: handles partial
// reads and treats EOF (0 bytes read) as an error. ERROR_MORE_DATA does not
// apply to byte-mode pipes, so a partial read simply continues the loop.
HRESULT IpcStreamReadAll(IpcHandle handle, uint8_t *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        // Clamp before narrowing: a remainder above 4 GiB must neither wrap to 0
        // (spinning forever) nor exceed the per-call limit.
        const size_t remaining = length - total;
        const DWORD chunk = static_cast<DWORD>(remaining < kMaxIoChunk ? remaining : kMaxIoChunk);

        DWORD bytesRead = 0;
        if (!ReadFile(handle, buffer + total, chunk, &bytesRead, nullptr))
        {
            LOGE(log << "DiagnosticIpc: ReadFile failed, error=" << GetLastError());
            return E_FAIL;
        }

        if (bytesRead == 0)
        {
            LOGE(log << "DiagnosticIpc: unexpected EOF while reading, got " << total << " of " << length);
            return E_FAIL;
        }

        total += bytesRead;
    }

    return S_OK;
}

// WriteFile loop until exactly `length` bytes are sent: handles partial writes.
HRESULT IpcStreamWriteAll(IpcHandle handle, const uint8_t *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        // Clamp before narrowing: a remainder above 4 GiB must neither wrap to 0
        // (spinning forever) nor exceed the per-call limit.
        const size_t remaining = length - total;
        const DWORD chunk = static_cast<DWORD>(remaining < kMaxIoChunk ? remaining : kMaxIoChunk);

        DWORD bytesWritten = 0;
        if (!WriteFile(handle, buffer + total, chunk, &bytesWritten, nullptr))
        {
            LOGE(log << "DiagnosticIpc: WriteFile failed, error=" << GetLastError());
            return E_FAIL;
        }

        if (bytesWritten == 0)
        {
            LOGE(log << "DiagnosticIpc: zero-byte write, got " << total << " of " << length);
            return E_FAIL;
        }

        total += bytesWritten;
    }

    return S_OK;
}

} // namespace dncdbg::DiagnosticIpc

#endif // _WIN32
