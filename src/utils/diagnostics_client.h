// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_DIAGNOSTICS_CLIENT_H
#define UTILS_DIAGNOSTICS_CLIENT_H

#include "utils/hresult.h"
#include <cstdint>

namespace dncdbg
{

// Minimal .NET Diagnostic IPC protocol client (protocol version DOTNET_IPC_V1).
//
// The protocol is connection-per-command: the runtime server handles exactly
// one IPC message per accepted connection and then closes the stream. Because
// of that this client is stateless — every call resolves the default
// diagnostics endpoint for the given PID and opens a fresh connection; there
// is no Connect/Disconnect lifecycle. The class holds no state, hence all
// methods are static.
//
// Not thread-sensitive: every call is independent.
class DiagnosticsClient
{
  public:

    // Resolves the default diagnostics endpoint for `pid` (Unix domain socket
    // on Linux/macOS, named pipe on Windows), sends the ResumeRuntime command
    // (command set 0x04, command id 0x01, empty payload) over a fresh
    // connection and validates the response. The server also closes its side
    // after the response, matching the one-command-per-connection model.
    //
    // Returns S_OK on success (server response 0xFF/0x00), the server's
    // HRESULT carried in the payload of an error response (0xFF/0xFF), or a
    // client-side HRESULT on transport / protocol failures.
    static HRESULT ResumeRuntime(uint32_t pid);
};

} // namespace dncdbg

#endif // UTILS_DIAGNOSTICS_CLIENT_H
