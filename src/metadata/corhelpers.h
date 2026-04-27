// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_CORHELPERS_H
#define METADATA_CORHELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/torelease.h"
#include <algorithm>
#include <cassert>

namespace dncdbg
{

// Helper to check bounds and return error if exceeded
inline HRESULT CheckBounds(PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd)
{
    if (pSig >= pSigEnd)
    {
        return META_E_BAD_SIGNATURE;
    }
    return S_OK;
}

inline HRESULT CorSigUncompressSkipOneByte_EndPtr(PCCOR_SIGNATURE &pSig,
                                                  PCCOR_SIGNATURE  pSigEnd)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    pSig++;
    return S_OK;
}

inline HRESULT CorSigUncompressCallingConv_EndPtr(PCCOR_SIGNATURE &pSig,
                                                  PCCOR_SIGNATURE  pSigEnd,
                                                  ULONG           &convOut)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    // Read 'calling convention' flags as 1 byte
    convOut = static_cast<ULONG>(*pSig);
    pSig++;

    return S_OK;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressData_EndPtr(PCCOR_SIGNATURE &pSig,
                                           PCCOR_SIGNATURE  pSigEnd,
                                           DWORD           &nDataOut)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    intptr_t cbDataSize = pSigEnd - pSig;
    // Compressed integer cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    auto dwDataSize = static_cast<DWORD>(cbDataSize);

    ULONG cbDataOutLength = 0;
    IfFailRet(CorSigUncompressData(pSig, dwDataSize, &nDataOut, &cbDataOutLength));
    pSig += cbDataOutLength;

    return S_OK;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressElementType_EndPtr(PCCOR_SIGNATURE &pSig,
                                                  PCCOR_SIGNATURE  pSigEnd,
                                                  CorElementType  &typeOut)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    // Read 'type' as 1 byte
    typeOut = static_cast<CorElementType>(*pSig);
    pSig++;

    return S_OK;
}

// part of diagnostics/src/shared/inc/corhlprpriv.h
inline HRESULT CorSigUncompressToken_EndPtr(PCCOR_SIGNATURE &pSig,
                                            PCCOR_SIGNATURE  pSigEnd,
                                            mdToken         &tkTokenOut)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    intptr_t cbDataSize = pSigEnd - pSig;
    // Compressed token cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    const auto dwDataSize = static_cast<DWORD>(cbDataSize);

    uint32_t cbTokenOutLength = 0;
    IfFailRet(CorSigUncompressToken(pSig, dwDataSize, &tkTokenOut, &cbTokenOutLength));
    pSig += cbTokenOutLength;

    return S_OK;
}

inline HRESULT CorSigUncompressSignedInt_EndPtr(PCCOR_SIGNATURE &pSig,
                                                PCCOR_SIGNATURE  pSigEnd,
                                                int             &intOut)
{
    HRESULT Status = S_OK;
    IfFailRet(CheckBounds(pSig, pSigEnd));

    ULONG cbDataOutLength = 0;
    ULONG iData = 0;

    intptr_t cbDataSize = pSigEnd - pSig;
    // Compressed integer cannot be bigger than 4 bytes
    cbDataSize = std::min(cbDataSize, static_cast<intptr_t>(4));
    auto dwDataSize = static_cast<DWORD>(cbDataSize);

    IfFailRet(CorSigUncompressData(pSig, dwDataSize, &iData, &cbDataOutLength));
    pSig += cbDataOutLength;

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
    assert(iData <= static_cast<ULONG>(std::numeric_limits<int>::max()));
    intOut = static_cast<int>(iData);
    return S_OK;
}

} // namespace dncdbg

#endif // METADATA_CORHELPERS_H
