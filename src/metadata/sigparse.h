// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_SIGPARSE_H
#define METADATA_SIGPARSE_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string>
#include <vector>
#include <utility>

namespace dncdbg
{

struct SigElementType
{
    CorElementType elemType{ELEMENT_TYPE_MAX};
    std::string metadataTypeName;
    // The TypeDef/TypeRef/TypeSpec token the element type was resolved from,
    // or mdTokenNil for element types that have no token.
    mdToken typeToken{mdTokenNil};
    std::string parameterModifier; // for example, `in`, `out` or `ref`
    CorElementType genericElemType{ELEMENT_TYPE_END};
    ULONG varNum{0};

    SigElementType() = default;
    SigElementType(CorElementType type, std::string name)
        : elemType(type),
          metadataTypeName(std::move(name))
    {
    }

    void Clear()
    {
        elemType = ELEMENT_TYPE_MAX;
        typeToken = mdTokenNil;
        metadataTypeName.clear();
        parameterModifier.clear();
        genericElemType = ELEMENT_TYPE_END;
        varNum = 0;
    }

    static bool isAlias(CorElementType elemType1, CorElementType elemType2, const std::string &name2);
    [[nodiscard]] bool areEqual(const SigElementType &arg) const;
    bool operator==(const SigElementType &arg) const
    {
        return areEqual(arg);
    }
    bool operator!=(const SigElementType &arg) const
    {
        return !areEqual(arg);
    }
};

// A parsed constant ("literal") field or local variable signature.
// Note, the constant value itself is not part of the FieldSig grammar: for fields it is stored
// in the metadata Constant table, while for local constants it is encoded in the signature blob
// right after the type.
struct SigConstant
{
    SigElementType elementType;          // the constant value type
    std::vector<std::string> args;       // "display" names of the parsed generic arguments
    PCCOR_SIGNATURE pRawValue{nullptr};  // the constant value encoded in the signature (local constants only), nullptr otherwise
    ULONG rawValueLength{0};             // the size of the constant value encoded in the signature, in bytes

    void Clear()
    {
        elementType.Clear();
        args.clear();
        pRawValue = nullptr;
        rawValueLength = 0;
    }
};

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE &pSig, PCCOR_SIGNATURE pSigEnd,
                         DWORD flags, SigElementType &sigElementType, std::vector<std::string> *args = nullptr,
                         bool addElementTypeName = false);

HRESULT ParseMethodSig(IMetaDataImport *pMDImport, mdMethodDef methodDef, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                       SigElementType &returnElementType, std::vector<SigElementType> &argElementTypes,
                       bool addElementTypeName = false, uint32_t *pMethodGenParamCount = nullptr);

HRESULT ParseFieldSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                      SigElementType &sigElementType, bool addElementTypeName = false,
                      std::vector<std::string> *args = nullptr);

HRESULT ParseLiteralFieldSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                             SigConstant &sigConstant);

HRESULT ParseLiteralLocalSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                             SigConstant &sigConstant);

HRESULT ApplyGenericTypeParameters(const std::vector<SigElementType> &genericTypeParameters, SigElementType &methodArg);
HRESULT ApplyGenericMethodParameters(const std::vector<SigElementType> &genericMethodParameters, SigElementType &methodArg);

HRESULT GetMethodArgCount(PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, uint32_t &count);

} // namespace dncdbg

#endif // METADATA_SIGPARSE_H
