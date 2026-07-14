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

} // namespace dncdbg
