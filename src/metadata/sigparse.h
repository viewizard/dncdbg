// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include <string>
#include <vector>

namespace dncdbg
{

struct SigElementType
{
    CorElementType corType = ELEMENT_TYPE_MAX;
    std::string typeName;

    SigElementType()
        : corType(ELEMENT_TYPE_MAX)
    {}

    SigElementType(CorElementType t, std::string n)
    {
        corType = t;
        typeName = n;
    }

    static bool isAlias(const CorElementType type1, const CorElementType type2, const std::string &name2);
    bool areEqual(const SigElementType &arg) const;
    inline bool operator==(const SigElementType &arg)
    {
        return areEqual(arg);
    }
    inline bool operator!=(const SigElementType &arg)
    {
        return !areEqual(arg);
    }
};

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE *ppSig, SigElementType &sigElementType,
                         const std::vector<SigElementType> &typeGenerics,
                         const std::vector<SigElementType> &methodGenerics, bool addCorTypeName = false);

HRESULT SigParse(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, const std::vector<SigElementType> &typeGenerics,
                 const std::vector<SigElementType> &methodGenerics, SigElementType &returnElementType,
                 std::vector<SigElementType> &argElementTypes, bool addCorTypeName = false);

void NameForTypeSig(PCCOR_SIGNATURE typePtr, ICorDebugType *pEnclosingType, IMetaDataImport *pMDImport, std::string &typeName);

} // namespace dncdbg
