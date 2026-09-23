// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/diagnostics_client.h"
#include "utils/diagnostics_ipc.h"
#include "utils/logger.h"
#include <array>
#include <iomanip>
#include <vector>

#ifdef FEATURE_PAL
#include <palrt.h> // S_OK, E_FAIL, FAILED (via pal.h chain)
#endif

namespace dncdbg::DiagnosticsClient
{

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
    // Internal transport machinery lives in DiagnosticsIpc (see utils/diagnostics_ipc.h).
    using namespace DiagnosticsIpc;

    HRESULT Status = S_OK;

    std::string endpoint;
    IfFailRet(IpcEndpointResolve(pid, endpoint));
    LOGD(log << "DiagnosticsIpc: resolved endpoint for PID " << pid << ": " << endpoint);

    IpcHandle handle = kInvalidIpcHandle;
    IfFailRet(IpcStreamOpen(endpoint, handle));
    const IpcStreamGuard streamGuard(handle);

    IpcHeader request{};
    MakeRequestHeader(kCommandSetProcess, kProcessCommandIdResumeRuntime, request);

    std::array<uint8_t, kHeaderSize> requestBytes{};
    SerializeHeader(request, requestBytes);

    LOGD(log << "DiagnosticsIpc: sending ResumeRuntime request to '" << endpoint << "'");
    IfFailRet(IpcStreamWriteAll(handle, requestBytes.data(), requestBytes.size()));

    // Response: read exactly 20 header bytes first, then the payload, if any.
    // A short read (EOF before the full header) is a protocol error.
    std::array<uint8_t, kHeaderSize> responseBytes{};
    Status = IpcStreamReadAll(handle, responseBytes.data(), responseBytes.size());
    if (FAILED(Status))
    {
        LOGE(log << "DiagnosticsIpc: failed to read response header from '" << endpoint << "'");
        return DS_IPC_E_BAD_ENCODING;
    }

    IpcHeader response{};
    size_t payloadSize = 0;
    Status = DecodeResponseHeader(responseBytes, response, payloadSize);
    if (FAILED(Status))
    {
        LOGE(log << "DiagnosticsIpc: response header validation failed from '" << endpoint << "'");
        return Status;
    }

    std::vector<uint8_t> payload(payloadSize, 0);
    if (payloadSize > 0)
    {
        Status = IpcStreamReadAll(handle, payload.data(), payload.size());
        if (FAILED(Status))
        {
            LOGE(log << "DiagnosticsIpc: failed to read response payload from '" << endpoint << "'");
            return DS_IPC_E_BAD_ENCODING;
        }
    }

    if (response.command_set == kCommandSetServer && response.command_id == kServerResponseIdOk)
    {
        LOGD(log << "DiagnosticsIpc: ResumeRuntime succeeded for PID " << pid);
        return S_OK;
    }

    if (response.command_set == kCommandSetServer && response.command_id == kServerResponseIdError)
    {
        if (payload.size() < sizeof(int32_t))
        {
            LOGE(log << "DiagnosticsIpc: error response payload is too small: " << payload.size());
            return DS_IPC_E_BAD_ENCODING;
        }

        // Return the server HRESULT verbatim.
        const auto serverStatus = static_cast<HRESULT>(DecodeInt32LE(payload.data()));
        LOGE(log << "DiagnosticsIpc: ResumeRuntime failed for PID " << pid << ", server HRESULT: 0x"
                 << std::setw(hexErrWidth) << std::setfill('0') << std::hex << serverStatus);
        return serverStatus;
    }

    LOGE(log << "DiagnosticsIpc: unexpected response, command set: 0x"
             << static_cast<unsigned int>(response.command_set) << ", command id: 0x"
             << static_cast<unsigned int>(response.command_id));
    return DS_IPC_E_BAD_ENCODING;
}

} // namespace dncdbg::DiagnosticsClient
