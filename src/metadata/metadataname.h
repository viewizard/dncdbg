// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_METADATANAME_H
#define METADATA_METADATANAME_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/hresult.h"
#include "utils/utf.h"
#include <array>
#include <vector>

namespace dncdbg::MetadataName
{

// Reads a metadata name into `nameUtf8`. `readFn(WCHAR *buf, ULONG size, ULONG *pNeeded)` must call the metadata API
// with the given buffer and size, and report the required length (including the null terminator) through `pNeeded`.
// Most names fit into the stack buffer, so the common case needs a single metadata call and no heap allocation.
// A longer name is read once more into a heap buffer of the exact size.
template <typename ReadFn>
HRESULT ReadUtf8(const ReadFn &readFn, std::string &nameUtf8)
{
    static constexpr ULONG stackNameChars = 128;

    HRESULT Status = S_OK;
    std::array<WCHAR, stackNameChars> stackName{};
    ULONG nameLen = 0; // Length including the null terminator.
    Status = readFn(stackName.data(), stackNameChars, &nameLen);
    if (nameLen <= stackNameChars)
    {
        if (FAILED(Status))
        {
            return Status;
        }
        nameUtf8 = to_utf8(stackName.data());
        return S_OK;
    }

    std::vector<WCHAR> heapName(nameLen, '\0');
    IfFailRet(readFn(heapName.data(), nameLen, nullptr));
    nameUtf8 = to_utf8(heapName.data());
    return S_OK;
}

} // namespace dncdbg::MetadataName

#endif // METADATA_METADATANAME_H
