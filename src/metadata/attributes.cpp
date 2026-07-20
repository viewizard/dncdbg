// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/attributes.h"
#include "metadata/typeprinter.h"
#include <algorithm>
#include <functional>

namespace dncdbg
{

namespace
{

using ForEachAttributeCallback = std::function<bool(const std::string &AttrName, const void *pBlob, ULONG cbBlob)>;

bool ForEachAttribute(IMetaDataImport *pMDImport, mdToken tok, const ForEachAttributeCallback &cb)
{
    bool found = false;
    ULONG numAttributes = 0;
    HCORENUM fEnum = nullptr;
    mdCustomAttribute attr = 0;
    while (SUCCEEDED(pMDImport->EnumCustomAttributes(&fEnum, tok, 0, &attr, 1, &numAttributes)) && numAttributes != 0)
    {
        std::string mdName;
        mdToken tkType = mdTokenNil;
        void const *pBlob = nullptr;
        ULONG cbBlob = 0;
        if (FAILED(pMDImport->GetCustomAttributeProps(attr, nullptr, &tkType, &pBlob, &cbBlob)) ||
            FAILED(TypePrinter::NameForToken(tkType, pMDImport, mdName, true, nullptr)))
        {
            continue;
        }

        found = cb(mdName, pBlob, cbBlob);
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

bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string_view attrName)
{
    return ForEachAttribute(pMDImport, tok,
        [&attrName](const std::string &AttrName, const void *, ULONG) -> bool
        {
            return AttrName == attrName;
        });
}

bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, const std::vector<std::string_view> &attrNames)
{
    return ForEachAttribute(pMDImport, tok,
        [&attrNames](const std::string &AttrName, const void *, ULONG) -> bool
        {
            return std::find(attrNames.begin(), attrNames.end(), AttrName) != attrNames.end();
        });
}

DebuggerBrowsableState GetDebuggerBrowsableAttributeState(IMetaDataImport *pMDImport, mdToken tok)
{
    DebuggerBrowsableState browsableState = DebuggerBrowsableState::Collapsed;

    ForEachAttribute(pMDImport, tok,
        [&](const std::string &attrName, const void *pBlob, ULONG cbBlob) -> bool
        {
            if (attrName != DebuggerAttribute::Browsable)
            {
                return false;
            }

            // In case of DebuggerBrowsableAttribute, blob size must be 8 bytes:
            // 2 bytes - blob prolog 0x0001
            // 4 bytes - data (DebuggerBrowsableAttribute::State), default enum type in C# (int)
            // 2 bytes - alignment
            static constexpr ULONG debuggerBrowsableAttributeBlobSize = 8;
            if (cbBlob != debuggerBrowsableAttributeBlobSize)
            {
                return false;
            }

            const auto *pbBlob = static_cast<const uint8_t *>(pBlob);

            // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
            // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
            if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
            {
                return false;
            }

            // Read the 4-byte data value in little-endian order, since metadata blobs are
            // always little-endian regardless of the host platform byte order.
            const uint32_t data = static_cast<uint32_t>(pbBlob[2]) |
                                  static_cast<uint32_t>(pbBlob[3]) << 8 |
                                  static_cast<uint32_t>(pbBlob[4]) << 16 |
                                  static_cast<uint32_t>(pbBlob[5]) << 24;
            browsableState = static_cast<DebuggerBrowsableState>(data);
            return true;
        });

    return browsableState;
}

bool HasDebuggerTypeProxyAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string &proxyTypeName)
{
    proxyTypeName.clear();

    return ForEachAttribute(pMDImport, tok,
        [&](const std::string &attrName, const void *pBlob, ULONG cbBlob) -> bool
        {
            if (attrName != DebuggerAttribute::TypeProxy)
            {
                return false;
            }

            const auto *pbBlob = static_cast<const uint8_t *>(pBlob);
            PCCOR_SIGNATURE pbBlobEnd = pbBlob + cbBlob;

            // In case of DebuggerTypeProxyAttribute, blob format is:
            // 2 bytes - blob prolog 0x0001
            // 1-4 bytes - type name string length (compressed unsigned integer)
            // N bytes - type name string data (UTF-8)
            // 2 bytes - named arguments count (for class or struct attributes, must be 0)
            // ... named arguments are not provided in this case

            // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
            // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
            if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
            {
                return false;
            }
            pbBlob += sizeof(uint16_t);

            std::string_view typeName;
            if (!ReadString(&pbBlob, pbBlobEnd, typeName))
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

            proxyTypeName = typeName;
            return true;
        });
}

bool HasAssemblyDebuggerTypeProxyAttribute(IMetaDataImport *pMDImport, mdToken tok, const std::string &detectTypeName, std::string &proxyTypeName)
{
    proxyTypeName.clear();

    return ForEachAttribute(pMDImport, tok,
        [&](const std::string &attrName, const void *pBlob, ULONG cbBlob) -> bool
        {
            if (attrName != DebuggerAttribute::TypeProxy)
            {
                return false;
            }

            const auto *pbBlob = static_cast<const uint8_t *>(pBlob);
            PCCOR_SIGNATURE pbBlobEnd = pbBlob + cbBlob;

            // In case of DebuggerTypeProxyAttribute with named arguments, blob format is:
            // 2 bytes - blob prolog 0x0001
            // 1-4 bytes - type name string length (compressed unsigned integer)
            // N bytes - type name string data (UTF-8)
            // 2 bytes - named arguments count
            // For each named argument:
            //   1 byte - CorSerializationType (SERIALIZATION_TYPE_FIELD, SERIALIZATION_TYPE_PROPERTY)
            //   1 byte - CorSerializationType (SERIALIZATION_TYPE_STRING for TargetTypeName, SERIALIZATION_TYPE_TYPE for Target)
            //   1-4 bytes - argument name length (compressed unsigned integer)
            //   K bytes - argument name string data (UTF-8)
            //   1-4 bytes - argument value length (compressed unsigned integer)
            //   M bytes - argument value string data (UTF-8)

            // Check blob prolog 0x0001 as bytes to avoid endianness and alignment issues.
            // Metadata blobs are always little-endian, so 0x0001 is stored as {0x01, 0x00}.
            if (pbBlob[0] != 0x01 || pbBlob[1] != 0x00)
            {
                return false;
            }
            pbBlob += sizeof(uint16_t);

            std::string_view typeName;
            if (!ReadString(&pbBlob, pbBlobEnd, typeName))
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

            if (detectTypeName == argumentValue)
            {
                proxyTypeName = typeName;
                return true;
            }

            return false;
        });
}

} // namespace dncdbg
