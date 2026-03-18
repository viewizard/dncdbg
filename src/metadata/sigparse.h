// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_SIGPARSE_H
#define METADATA_SIGPARSE_H

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
    CorElementType corType;
    std::string typeName;
    int elementType;
    ULONG varNum;

    SigElementType()
        : corType(ELEMENT_TYPE_MAX),
          elementType(ELEMENT_TYPE_END),
          varNum(0)
    {}

    SigElementType(CorElementType t, const std::string &n)
    {
        corType = t;
        typeName = n;
        elementType = ELEMENT_TYPE_END;
        varNum = 0;
    }

    static bool isAlias(const CorElementType type1, const CorElementType type2, const std::string &name2);
    [[nodiscard]] bool areEqual(const SigElementType &arg) const;
    bool operator==(const SigElementType &arg)
    {
        return areEqual(arg);
    }
    bool operator!=(const SigElementType &arg)
    {
        return !areEqual(arg);
    }
};

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE *ppSig, SigElementType &sigElementType, bool addCorTypeName = false);

HRESULT ParseMethodSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, SigElementType &returnElementType,
                       std::vector<SigElementType> &argElementTypes, bool addCorTypeName = false);

void TypeNameFromSig(PCCOR_SIGNATURE typePtr, ICorDebugType *pEnclosingType, IMetaDataImport *pMDImport,
                     std::string &typeName);

HRESULT ApplyTypeGenerics(const std::vector<SigElementType> &typeGenerics, SigElementType &methodArg);
HRESULT ApplyMethodGenerics(const std::vector<SigElementType> &methodGenerics, SigElementType &methodArg);

} // namespace dncdbg

#endif // METADATA_SIGPARSE_H
