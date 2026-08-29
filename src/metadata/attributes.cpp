// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include <algorithm>
#include <cassert>
#include <functional>

namespace dncdbg
{

namespace
{

using ForEachAttributeCallback = std::function<bool(const std::string &displayAttrName, const void *pBlob, ULONG cbBlob)>;

bool ForEachAttribute(IMetaDataImport *pMDImport, mdToken tok, const ForEachAttributeCallback &cb)
{
    bool found = false;
    ULONG numAttributes = 0;
    HCORENUM fEnum = nullptr;
    mdCustomAttribute customAttr = 0;
    while (SUCCEEDED(pMDImport->EnumCustomAttributes(&fEnum, tok, 0, &customAttr, 1, &numAttributes)) && numAttributes != 0)
    {
        std::string displayAttrName;
        mdToken attrToken = mdTokenNil;
        void const *pBlob = nullptr;
        ULONG cbBlob = 0;
        if (FAILED(pMDImport->GetCustomAttributeProps(customAttr, nullptr, &attrToken, &pBlob, &cbBlob)) ||
            FAILED(MetadataHelpers::GetFQDisplayNameForToken(attrToken, pMDImport, displayAttrName, nullptr)))
        {
            continue;
        }

        found = cb(displayAttrName, pBlob, cbBlob);
        if (found)
        {
            break;
        }
    }
    pMDImport->CloseEnum(fEnum);
    return found;
}

bool UncompressUint(const uint8_t *&pBlob, const uint8_t *pEnd, uint32_t &length)
{
    // ECMA-335 compressed unsigned integer encoding constants
    static constexpr uint8_t kCompressedUintSentinelMarker = 0xFF;
    static constexpr uint32_t kCompressedUintSentinelValue = 0xFFFFFFFF;
    static constexpr uint8_t kCompressedUint1ByteMask = 0x80;
    static constexpr uint8_t kCompressedUint2ByteMask = 0xC0;
    static constexpr uint8_t kCompressedUint2ByteMarker = 0x80;
    static constexpr uint8_t kCompressedUint2ByteBitsMask = 0x3F;
    static constexpr uint8_t kCompressedUint4ByteMask = 0xE0;
    static constexpr uint8_t kCompressedUint4ByteMarker = 0xC0;
    static constexpr uint8_t kCompressedUint4ByteBitsMask = 0x1F;
    static constexpr int kBitShift1Byte = 8;
    static constexpr int kBitShift2Bytes = 16;
    static constexpr int kBitShift3Bytes = 24;

    if (pBlob >= pEnd)
    {
        return false;
    }

    const uint8_t b1 = *pBlob++;
    if (b1 == kCompressedUintSentinelMarker)
    {
        length = kCompressedUintSentinelValue;
        return true;
    }
    if ((b1 & kCompressedUint1ByteMask) == 0)
    {
        length = b1;
        return true;
    }

    if (pBlob >= pEnd)
    {
        return false;
    }
    const uint8_t b2 = *pBlob++;
    if ((b1 & kCompressedUint2ByteMask) == kCompressedUint2ByteMarker)
    {
        length = ((b1 & kCompressedUint2ByteBitsMask) << kBitShift1Byte) | b2;
        return true;
    }

    if (pBlob + 2 > pEnd)
    {
        return false;
    }
    const uint8_t b3 = *pBlob++;
    const uint8_t b4 = *pBlob++;
    if ((b1 & kCompressedUint4ByteMask) == kCompressedUint4ByteMarker)
    {
        length = ((b1 & kCompressedUint4ByteBitsMask) << kBitShift3Bytes) | (b2 << kBitShift2Bytes) | (b3 << kBitShift1Byte) | b4;
        return true;
    }

    return false;
}

bool ReadString(const uint8_t **ppbBlob, const uint8_t *pbBlobEnd, std::string_view &result)
{
    uint32_t size = 0;
    if (!UncompressUint(*ppbBlob, pbBlobEnd, size))
    {
        return false;
    }

    // Ensure there are enough bytes for string.
    if (*ppbBlob + size > pbBlobEnd)
    {
        return false;
    }

    result = std::string_view(reinterpret_cast<const char *>(*ppbBlob), size);
    *ppbBlob += size;

    return true;
}

} // unnamed namespace

bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, const WSTRING &attrName)
{
    // Note, in case the attribute is not found, GetCustomAttributeByName() returns S_FALSE or an error code.
    return S_OK == pMDImport->GetCustomAttributeByName(tok, attrName.c_str(), nullptr, nullptr);
}

bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, const std::vector<WSTRING> &attrNames)
{
    return std::any_of(attrNames.cbegin(), attrNames.cend(),
        [&](const WSTRING &name)
        {
            return HasAttribute(pMDImport, tok, name);
        });
}

DebuggerBrowsableState GetDebuggerBrowsableAttributeState(IMetaDataImport *pMDImport, mdToken tok)
{
    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerbrowsableattribute
    // Determines if and how a member is displayed in the debugger variable windows.

    const void *pBlob = nullptr;
    ULONG cbBlob = 0;
    if (S_OK != pMDImport->GetCustomAttributeByName(tok, W("System.Diagnostics.DebuggerBrowsableAttribute"), &pBlob, &cbBlob))
    {
        return DebuggerBrowsableState::Collapsed;
    }

    // In case of DebuggerBrowsableAttribute, blob size must be 8 bytes:
    // 2 bytes - blob prolog 0x0001
    // 4 bytes - data (DebuggerBrowsableAttribute::State), default enum type in C# (int)
    // 2 bytes - alignment
    static constexpr ULONG debuggerBrowsableAttributeBlobSize = 8;
    if (cbBlob != debuggerBrowsableAttributeBlobSize)
    {
        return DebuggerBrowsableState::Collapsed;
    }

    const auto *pbBlob = static_cast<const uint8_t *>(pBlob);

    // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
    // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
    if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
    {
        return DebuggerBrowsableState::Collapsed;
    }

    // Read the 4-byte data value in little-endian order, since metadata blobs are
    // always little-endian regardless of the host platform byte order.
    const uint32_t data = static_cast<uint32_t>(pbBlob[2]) |
                            static_cast<uint32_t>(pbBlob[3]) << 8 |
                            static_cast<uint32_t>(pbBlob[4]) << 16 |
                            static_cast<uint32_t>(pbBlob[5]) << 24;

    return static_cast<DebuggerBrowsableState>(data);
}

bool HasAsyncStateMachineAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string &metadataStateMachineType)
{
    // https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.asyncstatemachineattribute
    // Indicates whether a method is marked with the async modifier.
    const void *pBlob = nullptr;
    ULONG cbBlob = 0;
    // Note, in case the attribute is not found, GetCustomAttributeByName() returns S_FALSE or an error code.
    if (S_OK != pMDImport->GetCustomAttributeByName(tok, W("System.Runtime.CompilerServices.AsyncStateMachineAttribute"), &pBlob, &cbBlob))
    {
        return false;
    }

    const auto *pbBlob = static_cast<const uint8_t *>(pBlob);
    PCCOR_SIGNATURE pbBlobEnd = pbBlob + cbBlob;

    // In case of AsyncStateMachineAttribute, blob format is:
    // 2 bytes - blob prolog 0x0001
    // 1-4 bytes - text string length (compressed unsigned integer)
    // N bytes - text string data (UTF-8)

    // Ensure there are enough bytes for the blob prolog before accessing it.
    if (cbBlob < sizeof(uint16_t))
    {
        return false;
    }

    // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
    // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
    if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
    {
        return false;
    }
    pbBlob += sizeof(uint16_t);

    std::string_view text;
    if (!ReadString(&pbBlob, pbBlobEnd, text))
    {
        return false;
    }

    metadataStateMachineType = text;
    return true;
}

bool HasDebuggerAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string_view attrName, std::string &output)
{
    assert(attrName == DebuggerAttribute::TypeProxy || attrName == DebuggerAttribute::Display);

    output.clear();

    return ForEachAttribute(pMDImport, tok,
        [&](const std::string &displayAttrName, const void *pBlob, ULONG cbBlob) -> bool
        {
            if (displayAttrName != attrName)
            {
                return false;
            }

            const auto *pbBlob = static_cast<const uint8_t *>(pBlob);
            PCCOR_SIGNATURE pbBlobEnd = pbBlob + cbBlob;

            // In case of DebuggerTypeProxyAttribute and DebuggerDisplayAttribute, blob format is:
            // 2 bytes - blob prolog 0x0001
            // 1-4 bytes - text string length (compressed unsigned integer)
            // N bytes - text string data (UTF-8)
            // 2 bytes - named arguments count
            // ... named arguments are not provided in this case

            // Ensure there are enough bytes for the blob prolog before accessing it.
            if (cbBlob < sizeof(uint16_t))
            {
                return false;
            }

            // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
            // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
            if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
            {
                return false;
            }
            pbBlob += sizeof(uint16_t);

            std::string_view text;
            if (!ReadString(&pbBlob, pbBlobEnd, text))
            {
                return false;
            }

            // Ensure there are enough bytes remaining.
            if (pbBlob + sizeof(uint16_t) > pbBlobEnd)
            {
                return false;
            }

            const uint16_t namedArguments = static_cast<uint16_t>(pbBlob[0]) |
                                            static_cast<uint16_t>(pbBlob[1]) << 8;
            if (namedArguments != 0)
            {
                return false;
            }

            output = text;
            return true;
        });
}

bool HasAssemblyDebuggerAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string_view attrName,
                                  const std::string &detectTypeName, std::string &output)
{
    assert(attrName == DebuggerAttribute::TypeProxy || attrName == DebuggerAttribute::Display);

    output.clear();

    return ForEachAttribute(pMDImport, tok,
        [&](const std::string &displayAttrName, const void *pBlob, ULONG cbBlob) -> bool
        {
            if (displayAttrName != attrName)
            {
                return false;
            }

            const auto *pbBlob = static_cast<const uint8_t *>(pBlob);
            PCCOR_SIGNATURE pbBlobEnd = pbBlob + cbBlob;

            // In case of DebuggerTypeProxyAttribute and DebuggerDisplayAttribute with named arguments, blob format is:
            // 2 bytes - blob prolog 0x0001
            // 1-4 bytes - text string length (compressed unsigned integer)
            // N bytes - text string data (UTF-8)
            // 2 bytes - named arguments count
            // For each named argument:
            //   1 byte - CorSerializationType (SERIALIZATION_TYPE_FIELD, SERIALIZATION_TYPE_PROPERTY)
            //   1 byte - CorSerializationType (SERIALIZATION_TYPE_STRING for TargetTypeName, SERIALIZATION_TYPE_TYPE for Target)
            //   1-4 bytes - argument name length (compressed unsigned integer)
            //   K bytes - argument name string data (UTF-8)
            //   1-4 bytes - argument value length (compressed unsigned integer)
            //   M bytes - argument value string data (UTF-8)

            // Ensure there are enough bytes for the blob prolog before accessing it.
            if (cbBlob < sizeof(uint16_t))
            {
                return false;
            }

            // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
            // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
            if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
            {
                return false;
            }
            pbBlob += sizeof(uint16_t);

            std::string_view text;
            if (!ReadString(&pbBlob, pbBlobEnd, text))
            {
                return false;
            }

            // Ensure there are enough bytes for the named argument count.
            if (pbBlob + sizeof(uint16_t) > pbBlobEnd)
            {
                return false;
            }

            const uint16_t namedArguments = static_cast<uint16_t>(pbBlob[0]) |
                                            static_cast<uint16_t>(pbBlob[1]) << 8;
            if (namedArguments != 1)
            {
                return false;
            }
            pbBlob += sizeof(uint16_t);

            // Ensure there are enough bytes remaining.
            if (pbBlob + sizeof(uint8_t) > pbBlobEnd)
            {
                return false;
            }

            if (pbBlob[0] != SERIALIZATION_TYPE_FIELD &&
                pbBlob[0] != SERIALIZATION_TYPE_PROPERTY)
            {
                return false;
            }
            pbBlob += sizeof(uint8_t);

            // Ensure there are enough bytes remaining.
            if (pbBlob + sizeof(uint8_t) > pbBlobEnd)
            {
                return false;
            }

            const auto sType = static_cast<CorSerializationType>(pbBlob[0]);
            if (sType != SERIALIZATION_TYPE_STRING &&
                sType != SERIALIZATION_TYPE_TYPE)
            {
                return false;
            }
            pbBlob += sizeof(uint8_t);

            std::string_view argumentName;
            if (!ReadString(&pbBlob, pbBlobEnd, argumentName))
            {
                return false;
            }

            if ((sType == SERIALIZATION_TYPE_STRING && argumentName != "TargetTypeName") &&
                (sType == SERIALIZATION_TYPE_TYPE && argumentName != "Target"))
            {
                return false;
            }

            std::string_view argumentValue;
            if (!ReadString(&pbBlob, pbBlobEnd, argumentValue))
            {
                return false;
            }

            if (detectTypeName != argumentValue)
            {
                return false;
            }

            output = text;
            return true;
        });
}

} // namespace dncdbg
