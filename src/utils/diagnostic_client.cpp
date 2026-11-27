// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/diagnostic_client.h"
#include "utils/diagnostic_ipc.h"
#include "utils/logger.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <string>

#ifdef FEATURE_PAL
#include <palrt.h> // S_OK, E_FAIL, FAILED (via pal.h chain)
#endif

namespace dncdbg::DiagnosticClient
{

namespace
{

// Largest payload prefix this client interprets: the little-endian HRESULT
// carried by an error response. Any bytes past this prefix are not interpreted
// and are drained and discarded.
constexpr size_t kMaxInterpretedPayload = sizeof(int32_t);

// Size of the fixed stack buffer used to drain the rest of the payload. The
// size advertised in the header never drives an allocation, so a bogus `size`
// value cannot grow the stack or heap usage of this call.
constexpr size_t kPayloadDrainChunk = 64;

} // unnamed namespace

// ResumeRuntime exchange (protocol version DOTNET_IPC_V1):
// - request: exactly 20 bytes (header only, empty payload), command set 0x04,
//   command id 0x01;
// - success response: 20 bytes, command set 0xFF, command id 0x00;
// - error response: 24 bytes, command set 0xFF, command id 0xFF, payload with
//   a little-endian int32_t HRESULT;
// - the server closes the connection after the response either way, so this
//   method performs a complete one-command-per-connection round trip.
HRESULT ResumeRuntime(uint32_t pid)
{
    // Internal transport machinery lives in DiagnosticIpc (see utils/diagnostic_ipc.h).
    using namespace DiagnosticIpc;

    HRESULT Status = S_OK;

    std::string endpoint;
    IfFailRet(IpcEndpointResolve(pid, endpoint));
    LOGD(log << "DiagnosticIpc: resolved endpoint for PID " << pid << ": " << endpoint);

    IpcHandle handle = kInvalidIpcHandle;
    IfFailRet(IpcStreamOpen(endpoint, handle));
    const IpcStreamGuard streamGuard(handle);

    // One buffer serves both directions. The request is fully written before
    // the response header is read into the same storage.
    std::array<uint8_t, kHeaderSize> headerBytes{};
    {
        IpcHeader request{};
        MakeRequestHeader(kCommandSetProcess, kProcessCommandIdResumeRuntime, request);
        SerializeHeader(request, headerBytes);
    }

    LOGD(log << "DiagnosticIpc: sending ResumeRuntime request to '" << endpoint << "'");
    IfFailRet(IpcStreamWriteAll(handle, headerBytes.data(), headerBytes.size()));

    // Response: read exactly 20 header bytes first, then the payload, if any.
    // A short read (EOF before the full header) is a protocol error.
    Status = IpcStreamReadAll(handle, headerBytes.data(), headerBytes.size());
    if (FAILED(Status))
    {
        LOGE(log << "DiagnosticIpc: failed to read response header from '" << endpoint << "'");
        return DS_IPC_E_BAD_ENCODING;
    }

    IpcHeader response{};
    size_t payloadSize = 0;
    Status = DecodeResponseHeader(headerBytes, response, payloadSize);
    if (FAILED(Status))
    {
        LOGE(log << "DiagnosticIpc: response header validation failed from '" << endpoint << "'");
        return Status;
    }

    // Only the leading HRESULT-sized prefix of the payload is interpreted and is
    // read into `payload`. Any remaining bytes are drained in fixed-size chunks.
    // The server closes the connection after the response, so the discarded bytes
    // do not matter, but they are still consumed to keep the exchange complete.
    std::array<uint8_t, kMaxInterpretedPayload> payload{};
    const size_t inspectedPayloadSize = std::min(payloadSize, payload.size());
    if (inspectedPayloadSize > 0)
    {
        Status = IpcStreamReadAll(handle, payload.data(), inspectedPayloadSize);
        if (FAILED(Status))
        {
            LOGE(log << "DiagnosticIpc: failed to read response payload from '" << endpoint << "', expected "
                     << payloadSize << " bytes");
            return DS_IPC_E_BAD_ENCODING;
        }
    }

    size_t undrainedPayloadSize = payloadSize - inspectedPayloadSize;
    if (undrainedPayloadSize > 0)
    {
        std::array<uint8_t, kPayloadDrainChunk> drainBuffer{};
        while (undrainedPayloadSize > 0)
        {
            const size_t drainSize = std::min(undrainedPayloadSize, drainBuffer.size());
            Status = IpcStreamReadAll(handle, drainBuffer.data(), drainSize);
            if (FAILED(Status))
            {
                LOGE(log << "DiagnosticIpc: failed to drain response payload from '" << endpoint << "', expected "
                         << payloadSize << " bytes");
                return DS_IPC_E_BAD_ENCODING;
            }
            undrainedPayloadSize -= drainSize;
        }
    }

    if (response.commandSet == kCommandSetServer && response.commandId == kServerResponseIdOk)
    {
        LOGD(log << "DiagnosticIpc: ResumeRuntime succeeded for PID " << pid);
        return S_OK;
    }

    if (response.commandSet == kCommandSetServer && response.commandId == kServerResponseIdError)
    {
        if (payloadSize < kMaxInterpretedPayload)
        {
            LOGE(log << "DiagnosticIpc: error response payload is too small: " << payloadSize);
            return DS_IPC_E_BAD_ENCODING;
        }

        // Return the server HRESULT verbatim.
        const auto serverStatus = static_cast<HRESULT>(DecodeInt32LE(payload.data()));
        // Cast to uint32_t so a negative HRESULT prints as its 8-digit unsigned
        // hex form instead of being sign-extended.
        LOGE(log << "DiagnosticIpc: ResumeRuntime failed for PID " << pid << ", server HRESULT: 0x"
                 << std::setw(hexErrWidth) << std::setfill('0') << std::hex
                 << static_cast<uint32_t>(serverStatus));
        return serverStatus;
    }

    LOGE(log << "DiagnosticIpc: unexpected response, command set: 0x"
             << static_cast<unsigned int>(response.commandSet) << ", command id: 0x"
             << static_cast<unsigned int>(response.commandId));
    return DS_IPC_E_BAD_ENCODING;
}

} // namespace dncdbg::DiagnosticClient
