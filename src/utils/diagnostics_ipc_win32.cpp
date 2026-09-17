// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/diagnostics_ipc.h"
#include "utils/logger.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <string>

namespace dncdbg::DiagnosticsIpc
{

namespace
{

// The runtime creates its default diagnostics endpoint as a named pipe with a
// deterministic name "\\.\pipe\dotnet-diagnostic-{PID}", so no scanning is
// needed on Windows.
constexpr const char *kPipeNameFormat = "\\\\.\\pipe\\dotnet-diagnostic-%u";
constexpr size_t kPipeNameMax = 64;

} // unnamed namespace

// Endpoint discovery: format the deterministic pipe name for `pid`.
// Always succeeds for a syntactically valid PID.
HRESULT IpcEndpointResolve(uint32_t pid, std::string &outEndpoint)
{
    outEndpoint.clear();

    std::array<char, kPipeNameMax> name{};
    const int written = std::snprintf(name.data(), name.size(), kPipeNameFormat, static_cast<unsigned int>(pid));
    if (written <= 0 || static_cast<size_t>(written) >= name.size())
    {
        LOGE(log << "DiagnosticsIpc: failed to format pipe name for PID " << pid);
        return E_FAIL;
    }

    outEndpoint.assign(name.data(), static_cast<size_t>(written));
    return S_OK;
}

// Connects to the byte-mode named pipe `endpoint` (the server side is created
// with PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, a synchronous client handle
// is valid against such instance). Behavior mirrors the C# NamedPipeClientStream:
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
            LOGD(log << "DiagnosticsIpc: pipe does not exist: " << endpoint);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        if (error != ERROR_PIPE_BUSY)
        {
            LOGE(log << "DiagnosticsIpc: CreateFileA failed for '" << endpoint << "', error=" << error);
            return E_FAIL;
        }

        // All pipe instances are busy: wait until one becomes available or the
        // overall deadline elapses, then re-attempt CreateFileA.
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            LOGE(log << "DiagnosticsIpc: connect timed out for '" << endpoint << "'");
            return HRESULT_FROM_WIN32(ERROR_PIPE_BUSY);
        }

        const DWORD remaining = static_cast<DWORD>(deadline - now);
        if (!WaitNamedPipeA(endpoint.c_str(), remaining))
        {
            LOGE(log << "DiagnosticsIpc: WaitNamedPipeA failed for '" << endpoint << "', error=" << GetLastError());
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
// reads and treats EOF (0 bytes read) as an error. ERROR_MORE_DATA cannot
// occur on a byte-mode pipe, but the loop tolerates extra data defensively.
HRESULT IpcStreamReadAll(IpcHandle handle, uint8_t *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        // DWORD is 32-bit; keep single ReadFile calls within its range.
        DWORD chunk = static_cast<DWORD>(length - total);
        constexpr DWORD kMaxChunk = 0x40000000; // 1 GiB
        if (chunk > kMaxChunk)
        {
            chunk = kMaxChunk;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(handle, buffer + total, chunk, &bytesRead, nullptr))
        {
            LOGE(log << "DiagnosticsIpc: ReadFile failed, error=" << GetLastError());
            return E_FAIL;
        }

        if (bytesRead == 0)
        {
            LOGE(log << "DiagnosticsIpc: unexpected EOF while reading, got " << total << " of " << length);
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
        // DWORD is 32-bit; keep single WriteFile calls within its range.
        DWORD chunk = static_cast<DWORD>(length - total);
        constexpr DWORD kMaxChunk = 0x40000000; // 1 GiB
        if (chunk > kMaxChunk)
        {
            chunk = kMaxChunk;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(handle, buffer + total, chunk, &bytesWritten, nullptr))
        {
            LOGE(log << "DiagnosticsIpc: WriteFile failed, error=" << GetLastError());
            return E_FAIL;
        }

        total += bytesWritten;
    }

    return S_OK;
}

} // namespace dncdbg::DiagnosticsIpc

#endif // _WIN32
