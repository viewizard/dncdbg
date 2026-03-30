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
#include <utility>

namespace dncdbg
{

struct SigElementType
{
    CorElementType corType{ELEMENT_TYPE_MAX};
    std::string typeName;
    CorElementType elementType{ELEMENT_TYPE_END};
    ULONG varNum{0};

    SigElementType() = default;
    SigElementType(CorElementType t, std::string n)
        : corType(t),
          typeName(std::move(n))
    {
    }

    static bool isAlias(CorElementType type1, CorElementType type2, const std::string &name2);
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

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE &pSig, PCCOR_SIGNATURE pSigEnd,
                         SigElementType &sigElementType, bool addCorTypeName = false);

HRESULT ParseMethodSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, SigElementType &returnElementType,
                       std::vector<SigElementType> &argElementTypes, bool addCorTypeName = false);

HRESULT ApplyTypeGenerics(const std::vector<SigElementType> &typeGenerics, SigElementType &methodArg);
HRESULT ApplyMethodGenerics(const std::vector<SigElementType> &methodGenerics, SigElementType &methodArg);

} // namespace dncdbg

#endif // METADATA_SIGPARSE_H
