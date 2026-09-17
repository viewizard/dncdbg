// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_DIAGNOSTICS_IPC_H
#define UTILS_DIAGNOSTICS_IPC_H

#include "utils/hresult.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dncdbg::DiagnosticsIpc
{

// Internal machinery of the .NET Diagnostic IPC protocol client (protocol
// version DOTNET_IPC_V1): wire types, framing constants and the platform
// transport layer. The public entry point is DiagnosticsClient (see
// utils/diagnostics_client.h).

// Opaque transport handle. Defined here so the header stays platform-neutral.
#ifdef _WIN32
using IpcHandle = void *; // HANDLE; nullptr == invalid
#else
using IpcHandle = int;    // file descriptor; -1 == invalid
#endif

#ifdef _WIN32
constexpr IpcHandle kInvalidIpcHandle = nullptr;
#else
constexpr IpcHandle kInvalidIpcHandle = -1;
#endif

// Connect timeout (matches the C# client's ConnectTimeout) and per-phase I/O
// timeout for reads / writes.
constexpr unsigned kConnectTimeoutMs = 30000;
constexpr unsigned kIoTimeoutMs = 30000;

// Wire header size in bytes and magic size in bytes.
constexpr size_t kHeaderSize = 20;
constexpr size_t kMagicSize = 14;

// Command sets and command / response identifiers on the wire.
constexpr uint8_t kCommandSetServer = 0xFF;
constexpr uint8_t kCommandSetProcess = 0x04;
constexpr uint8_t kServerResponseIdOk = 0x00;
constexpr uint8_t kServerResponseIdError = 0xFF;
constexpr uint8_t kProcessCommandIdResumeRuntime = 0x01;

// Server -> client error HRESULTs carried in the payload of an error response
// (command set 0xFF, command id 0xFF, payload: little-endian int32_t HRESULT).
constexpr HRESULT DS_IPC_E_BAD_ENCODING = static_cast<HRESULT>(0x80131384);
constexpr HRESULT DS_IPC_E_UNKNOWN_COMMAND = static_cast<HRESULT>(0x80131385);
constexpr HRESULT DS_IPC_E_UNKNOWN_MAGIC = static_cast<HRESULT>(0x80131386);
constexpr HRESULT DS_IPC_E_NOTSUPPORTED = static_cast<HRESULT>(0x80131515);
constexpr HRESULT DS_IPC_E_FAIL = static_cast<HRESULT>(0x80004005);
constexpr HRESULT DS_IPC_E_NOT_YET_AVAILABLE = static_cast<HRESULT>(0x8013135b);
constexpr HRESULT DS_IPC_E_RUNTIME_UNINITIALIZED = static_cast<HRESULT>(0x80131371);
constexpr HRESULT DS_IPC_E_INVALIDARG = static_cast<HRESULT>(0x80070057);
constexpr HRESULT DS_IPC_E_INSUFFICIENT_BUFFER = static_cast<HRESULT>(0x8007007A);
constexpr HRESULT DS_IPC_E_ENVVAR_NOT_FOUND = static_cast<HRESULT>(0x800000CB);

// Wire header: 20 bytes on the wire, all integers little-endian.
// Serialize manually field-by-field with explicit little-endian encoding (see
// diagnostics_ipc.cpp); do NOT memcpy the struct (padding / endianness pitfalls).
struct IpcHeader
{
    std::array<uint8_t, kMagicSize> magic; // ASCII "DOTNET_IPC_V1" + '\0'
    uint16_t size;                         // total packet size = sizeof(header) + payload size
    uint8_t command_set;
    uint8_t command_id;
    uint16_t reserved;                     // must be 0x0000 in V1
};
static_assert(sizeof(IpcHeader) == kHeaderSize, "IpcHeader must be 20 bytes");

// Platform layer. Implemented in diagnostics_ipc_win32.cpp / diagnostics_ipc_unix.cpp.

// Resolves the default diagnostics endpoint for `pid` into `outEndpoint`
// (pipe name on Windows, absolute socket path on Unix). Returns
// HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) when no endpoint exists for `pid`.
HRESULT IpcEndpointResolve(uint32_t pid, std::string &outEndpoint);

// Opens a duplex byte-stream connection to `endpoint`, honoring
// kConnectTimeoutMs. The transport is a plain byte stream: there is no
// length-prefix or datagram framing, the header `size` field is the only
// framing on top of it.
HRESULT IpcStreamOpen(const std::string &endpoint, IpcHandle &outHandle);

// Closes the handle. Must tolerate being called once with any returned handle.
void IpcStreamClose(IpcHandle handle);

// Reads exactly `length` bytes or fails. Loops on partial reads / EINTR.
HRESULT IpcStreamReadAll(IpcHandle handle, uint8_t *buffer, size_t length);

// Writes exactly `length` bytes or fails. Loops on partial writes / EINTR.
HRESULT IpcStreamWriteAll(IpcHandle handle, const uint8_t *buffer, size_t length);

// RAII guard closing the transport stream on scope exit, including error paths.
// Must be declared after IpcStreamClose (the in-class destructor body calls it).
class IpcStreamGuard
{
  public:

    explicit IpcStreamGuard(IpcHandle handle) : m_handle(handle)
    {
    }

    ~IpcStreamGuard()
    {
        IpcStreamClose(m_handle);
    }

    IpcStreamGuard(const IpcStreamGuard &) = delete;
    IpcStreamGuard &operator=(const IpcStreamGuard &) = delete;
    IpcStreamGuard(IpcStreamGuard &&) = delete;
    IpcStreamGuard &operator=(IpcStreamGuard &&) = delete;

  private:

    IpcHandle m_handle;
};

// Shared header (de)serialization and response validation helpers.
// Implemented in diagnostics_ipc.cpp.

// Fills `outHeader` for a payload-less command request: wire magic, total size
// = kHeaderSize, `commandSet` / `commandId`, reserved = 0.
void MakeRequestHeader(uint8_t commandSet, uint8_t commandId, IpcHeader &outHeader);

// Serializes `header` into `out` using explicit little-endian encoding.
void SerializeHeader(const IpcHeader &header, std::array<uint8_t, kHeaderSize> &out);

// Decodes a little-endian wire header from `bytes` and validates it (wire
// magic match, size >= kHeaderSize). Returns S_OK or DS_IPC_E_BAD_ENCODING.
// On success `outHeader` holds the decoded fields and `outPayloadSize` holds
// the payload size (header.size - kHeaderSize).
HRESULT DecodeResponseHeader(const std::array<uint8_t, kHeaderSize> &bytes, IpcHeader &outHeader, size_t &outPayloadSize);

// Decodes a little-endian int32_t value (server HRESULT in an error response
// payload). HRESULT is a 4 bytes signed type in .NET, count on this.
int32_t DecodeInt32LE(const uint8_t *data);

} // namespace dncdbg::DiagnosticsIpc

#endif // UTILS_DIAGNOSTICS_IPC_H
