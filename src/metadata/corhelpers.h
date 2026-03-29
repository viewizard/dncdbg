// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef METADATA_CORHELPERS_H
#define METADATA_CORHELPERS_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "utils/torelease.h"
#include <algorithm>
#include <cassert>

namespace dncdbg
{

inline void CorSigUncompressSkipOneByte(PCCOR_SIGNATURE &pData)
{
    pData++;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressData_EndPtr(PCCOR_SIGNATURE &pData,
                                           PCCOR_SIGNATURE  pDataEnd,
                                           DWORD           *pnDataOut)
{
    assert(pData <= pDataEnd);

    intptr_t cbDataSize = pDataEnd - pData;
    // Compressed integer cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    auto dwDataSize = static_cast<DWORD>(cbDataSize);

    HRESULT Status = S_OK;
    ULONG cbDataOutLength = 0;
    IfFailRet(CorSigUncompressData(pData, dwDataSize, pnDataOut, &cbDataOutLength));
    pData += cbDataOutLength;

    return S_OK;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressElementType_EndPtr(PCCOR_SIGNATURE &pData,
                                                  PCCOR_SIGNATURE  pDataEnd,
                                                  CorElementType  *pTypeOut)
{
    assert(pData <= pDataEnd);

    if (pData >= pDataEnd)
    {   // No data
        return META_E_BAD_SIGNATURE;
    }
    // Read 'type' as 1 byte
    *pTypeOut = static_cast<CorElementType>(*pData);
    pData++;

    return S_OK;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressToken_EndPtr(PCCOR_SIGNATURE &pData,
                                            PCCOR_SIGNATURE  pDataEnd,
                                            mdToken         *ptkTokenOut)
{
    assert(pData <= pDataEnd);

    INT_PTR cbDataSize = pDataEnd - pData;
    // Compressed token cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    const auto dwDataSize = static_cast<DWORD>(cbDataSize);

    HRESULT Status = S_OK;
    uint32_t cbTokenOutLength = 0;
    IfFailRet(CorSigUncompressToken(pData, dwDataSize, ptkTokenOut, &cbTokenOutLength));
    pData += cbTokenOutLength;

    return S_OK;
}

inline HRESULT CorSigUncompressSignedInt_EndPtr(PCCOR_SIGNATURE &pData,
                                                PCCOR_SIGNATURE  pDataEnd,
                                                int             *pInt)
{
    assert(pData <= pDataEnd);

    ULONG cbDataOutLength = 0;
    ULONG iData = 0;

    intptr_t cbDataSize = pDataEnd - pData;
    // Compressed integer cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    auto dwDataSize = static_cast<DWORD>(cbDataSize);

    if (FAILED(CorSigUncompressData(pData, dwDataSize, &iData, &cbDataOutLength)))
    {
        *pInt = 0;
        return META_E_BAD_SIGNATURE;
    }
    pData += cbDataOutLength;

    const ULONG ulSigned = iData & 0x1;
    iData = iData >> 1;
    if (ulSigned != 0U)
    {
        if (cbDataOutLength == 1)
        {
            iData |= SIGN_MASK_ONEBYTE;
        }
        else if (cbDataOutLength == 2)
        {
            iData |= SIGN_MASK_TWOBYTE;
        }
        else
        {
            iData |= SIGN_MASK_FOURBYTE;
        }
    }
    *pInt = static_cast<int>(iData);
    return S_OK;
}

} // namespace dncdbg

#endif // METADATA_CORHELPERS_H