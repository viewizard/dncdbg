// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/diagnostics_ipc.h"
#include <algorithm>
#include <cstring>

#ifdef FEATURE_PAL
#include <palrt.h> // S_OK, E_FAIL, FAILED, HRESULT_FROM_WIN32 (via pal.h chain)
#endif

namespace dncdbg::DiagnosticsIpc
{

namespace
{

// Wire magic: ASCII "DOTNET_IPC_V1" + '\0' (13 chars + NUL = kMagicSize bytes).
constexpr std::array<uint8_t, kMagicSize> kMagicFull
{
    'D', 'O', 'T', 'N', 'E', 'T', '_', 'I', 'P', 'C', '_', 'V', '1', 0x00
};

// Field offsets in the little-endian wire representation of IpcHeader.
constexpr size_t kOffsetSize = kMagicSize;                               // uint16_t size
constexpr size_t kOffsetCommandSet = kOffsetSize + sizeof(uint16_t);     // uint8_t command_set
constexpr size_t kOffsetCommandId = kOffsetCommandSet + sizeof(uint8_t); // uint8_t command_id
constexpr size_t kOffsetReserved = kOffsetCommandId + sizeof(uint8_t);   // uint16_t reserved

static_assert(kOffsetReserved + sizeof(uint16_t) == kHeaderSize, "Unexpected wire header layout");

// Bit shifts for the individual bytes of little-endian 16 / 32 bit fields.
constexpr uint32_t kByteShiftLow = 8U;
constexpr uint32_t kByteShiftMid = 16U;
constexpr uint32_t kByteShiftHigh = 24U;

// Mask to extract the low byte of a multi-byte little-endian field.
constexpr uint32_t kByteMask = 0xFFU;

} // unnamed namespace

// Fills `outHeader` for a payload-less command request: wire magic, total size
// equal to the header size, given command set / command id, reserved = 0.
void MakeRequestHeader(uint8_t commandSet, uint8_t commandId, IpcHeader &outHeader)
{
    std::copy(kMagicFull.begin(), kMagicFull.end(), outHeader.magic.begin());
    outHeader.size = static_cast<uint16_t>(kHeaderSize);
    outHeader.command_set = commandSet;
    outHeader.command_id = commandId;
    outHeader.reserved = 0;
}

// Serializes `header` into `out` byte-by-byte with explicit little-endian
// encoding; the struct must never be memcpy'ed to the wire (padding and
// host endianness pitfalls).
void SerializeHeader(const IpcHeader &header, std::array<uint8_t, kHeaderSize> &out)
{
    std::copy(header.magic.begin(), header.magic.end(), out.begin());

    uint8_t *pBytes = out.data();
    pBytes[kOffsetSize] = static_cast<uint8_t>(static_cast<uint32_t>(header.size) & kByteMask);
    pBytes[kOffsetSize + 1] = static_cast<uint8_t>((static_cast<uint32_t>(header.size) >> kByteShiftLow) & kByteMask);
    pBytes[kOffsetCommandSet] = header.command_set;
    pBytes[kOffsetCommandId] = header.command_id;
    pBytes[kOffsetReserved] = static_cast<uint8_t>(static_cast<uint32_t>(header.reserved) & kByteMask);
    pBytes[kOffsetReserved + 1] = static_cast<uint8_t>((static_cast<uint32_t>(header.reserved) >> kByteShiftLow) & kByteMask);
}

// Decodes a little-endian wire header from `bytes` and validates it:
// - the magic must match "DOTNET_IPC_V1" + '\0';
// - the total size field must not be smaller than the header itself
//   (`size` counts header + payload).
// Returns S_OK or DS_IPC_E_BAD_ENCODING. On success `outHeader` holds the
// decoded fields and `outPayloadSize` holds the payload size
// (header.size - kHeaderSize, can be 0 for payload-less responses).
HRESULT DecodeResponseHeader(const std::array<uint8_t, kHeaderSize> &bytes, IpcHeader &outHeader, size_t &outPayloadSize)
{
    outPayloadSize = 0;

    const uint8_t *pBytes = bytes.data();
    std::copy_n(pBytes, kMagicSize, outHeader.magic.begin());

    outHeader.size = static_cast<uint16_t>(static_cast<uint32_t>(pBytes[kOffsetSize]) |
                     (static_cast<uint32_t>(pBytes[kOffsetSize + 1]) << kByteShiftLow));
    outHeader.command_set = pBytes[kOffsetCommandSet];
    outHeader.command_id = pBytes[kOffsetCommandId];
    outHeader.reserved = static_cast<uint16_t>(static_cast<uint32_t>(pBytes[kOffsetReserved]) |
                         (static_cast<uint32_t>(pBytes[kOffsetReserved + 1]) << kByteShiftLow));

    if (std::memcmp(outHeader.magic.data(), kMagicFull.data(), kMagicSize) != 0)
    {
        return DS_IPC_E_BAD_ENCODING;
    }

    if (static_cast<size_t>(outHeader.size) < kHeaderSize)
    {
        return DS_IPC_E_BAD_ENCODING;
    }

    outPayloadSize = static_cast<size_t>(outHeader.size) - kHeaderSize;
    return S_OK;
}

// Decodes a little-endian int32_t value (server HRESULT in an error response
// payload). HRESULT is a 4 bytes signed type in .NET, count on this.
int32_t DecodeInt32LE(const uint8_t *data)
{
    const uint32_t value = static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << kByteShiftLow) |
        (static_cast<uint32_t>(data[2]) << kByteShiftMid) |
        (static_cast<uint32_t>(data[3]) << kByteShiftHigh);

    return static_cast<int32_t>(value);
}

} // namespace dncdbg::DiagnosticsIpc
