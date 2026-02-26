// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/sigparse.h"
#include "metadata/typeprinter.h"
#include "utils/torelease.h"

namespace dncdbg
{

namespace
{

// https://github.com/dotnet/runtime/blob/57bfe474518ab5b7cfe6bf7424a79ce3af9d6657/docs/design/coreclr/profiling/davbr-blog-archive/samples/sigparse.cpp
constexpr ULONG SIG_METHOD_VARARG = 0x5;   // vararg calling convention
constexpr ULONG SIG_METHOD_GENERIC = 0x10; // used to indicate that the method has one or more generic parameters.

void GetCorTypeName(ULONG corType, std::string &typeName)
{
    switch (corType)
    {
    case ELEMENT_TYPE_VOID:
        typeName = "void";
        break;
    case ELEMENT_TYPE_BOOLEAN:
        typeName = "bool";
        break;
    case ELEMENT_TYPE_CHAR:
        typeName = "char";
        break;
    case ELEMENT_TYPE_I1:
        typeName = "sbyte";
        break;
    case ELEMENT_TYPE_U1:
        typeName = "byte";
        break;
    case ELEMENT_TYPE_I2:
        typeName = "short";
        break;
    case ELEMENT_TYPE_U2:
        typeName = "ushort";
        break;
    case ELEMENT_TYPE_I4:
        typeName = "int";
        break;
    case ELEMENT_TYPE_U4:
        typeName = "uint";
        break;
    case ELEMENT_TYPE_I8:
        typeName = "long";
        break;
    case ELEMENT_TYPE_U8:
        typeName = "ulong";
        break;
    case ELEMENT_TYPE_R4:
        typeName = "float";
        break;
    case ELEMENT_TYPE_R8:
        typeName = "double";
        break;
    case ELEMENT_TYPE_STRING:
        typeName = "string";
        break;
    case ELEMENT_TYPE_OBJECT:
        typeName = "object";
        break;
    default:
        typeName = "";
        break;
    }
}

// From sildasm.cpp
PCCOR_SIGNATURE NameForTypeSig(PCCOR_SIGNATURE typePtr, const std::vector<std::string> &args,
                               IMetaDataImport *pMDImport, std::string &out, std::string &appendix)
{
    mdToken tk = mdTokenNil;
    int typ = 0;
    ULONG n = 0;

    auto getGetNameWithAppendix = [&](const char *str) -> std::string
    {
        std::string subAppendix;
        typePtr = NameForTypeSig(typePtr, args, pMDImport, out, subAppendix);
        return str + subAppendix;
    };

    switch (typ = CorSigUncompressElementType(typePtr))
    {
    case ELEMENT_TYPE_VOID:
        out = "void";
        break;
    case ELEMENT_TYPE_BOOLEAN:
        out = "bool";
        break;
    case ELEMENT_TYPE_CHAR:
        out = "char";
        break;
    case ELEMENT_TYPE_I1:
        out = "sbyte";
        break;
    case ELEMENT_TYPE_U1:
        out = "byte";
        break;
    case ELEMENT_TYPE_I2:
        out = "short";
        break;
    case ELEMENT_TYPE_U2:
        out = "ushort";
        break;
    case ELEMENT_TYPE_I4:
        out = "int";
        break;
    case ELEMENT_TYPE_U4:
        out = "uint";
        break;
    case ELEMENT_TYPE_I8:
        out = "long";
        break;
    case ELEMENT_TYPE_U8:
        out = "ulong";
        break;
    case ELEMENT_TYPE_R4:
        out = "float";
        break;
    case ELEMENT_TYPE_R8:
        out = "double";
        break;
    case ELEMENT_TYPE_U:
        out = "UIntPtr";
        break;
    case ELEMENT_TYPE_I:
        out = "IntPtr";
        break;
    case ELEMENT_TYPE_OBJECT:
        out = "object";
        break;
    case ELEMENT_TYPE_STRING:
        out = "string";
        break;
    case ELEMENT_TYPE_TYPEDBYREF:
        out = "typedref";
        break;

    case ELEMENT_TYPE_VALUETYPE:
    case ELEMENT_TYPE_CLASS:
    {
        typePtr += CorSigUncompressToken(typePtr, &tk);
        TypePrinter::NameForToken(tk, pMDImport, out, true, nullptr);
    }
    break;

    case ELEMENT_TYPE_SZARRAY:
    {
        std::string subAppendix;
        typePtr = NameForTypeSig(typePtr, args, pMDImport, out, subAppendix);
        appendix = "[]" + subAppendix;
        break;
    }

    case ELEMENT_TYPE_ARRAY:
    {
        std::string subAppendix;
        typePtr = NameForTypeSig(typePtr, args, pMDImport, out, subAppendix);
        std::string newAppendix;
        const unsigned rank = CorSigUncompressData(typePtr);
        // <TODO> what is the syntax for the rank 0 case? </TODO>
        if (rank == 0)
        {
            newAppendix += "[BAD: RANK == 0!]";
        }
        else
        {
            std::vector<int> lowerBounds(rank, 0);
            std::vector<ULONG> sizes(rank, 0);

            const unsigned numSizes = CorSigUncompressData(typePtr);
            assert(numSizes <= rank);
            for (unsigned i = 0; i < numSizes; i++)
            {
                sizes[i] = CorSigUncompressData(typePtr);
            }

            const unsigned numLowBounds = CorSigUncompressData(typePtr);
            assert(numLowBounds <= rank);
            for (unsigned i = 0; i < numLowBounds; i++)
            {
                typePtr += CorSigUncompressSignedInt(typePtr, &lowerBounds[i]);
            }

            newAppendix += '[';
            if (rank == 1 && numSizes == 0 && numLowBounds == 0)
            {
                newAppendix += "..";
            }
            else
            {
                for (unsigned i = 0; i < rank; i++)
                {
                    // if (sizes[i] != 0 || lowerBounds[i] != 0)
                    // {
                    //     if (i < numSizes && lowerBounds[i] == 0)
                    //         out += std::to_string(sizes[i]);
                    //     else
                    //     {
                    //         if(i < numLowBounds)
                    //         {
                    //             newAppendix +=  std::to_string(lowerBounds[i]);
                    //             newAppendix += "..";
                    //             if (/*sizes[i] != 0 && */i < numSizes)
                    //                 newAppendix += std::to_string(lowerBounds[i] + sizes[i] - 1);
                    //         }
                    //     }
                    // }
                    if (i < rank - 1)
                    {
                        newAppendix += ',';
                    }
                }
            }
            newAppendix += ']';
        }
        appendix = newAppendix + subAppendix;
        break;
    }

    case ELEMENT_TYPE_VAR:
        n = CorSigUncompressData(typePtr);
        out = n < static_cast<ULONG>(args.size()) ? args.at(n) : "!" + std::to_string(n);
        break;

    case ELEMENT_TYPE_MVAR:
        out += "!!";
        n = CorSigUncompressData(typePtr);
        out += std::to_string(n);
        break;

    case ELEMENT_TYPE_FNPTR:
        out = "method ";
        out += "METHOD"; // was: typePtr = PrettyPrintSignature(typePtr, 0x7FFF, "*", out, pIMDI, nullptr);
        break;

    case ELEMENT_TYPE_GENERICINST:
    {
        // typePtr = NameForTypeSig(typePtr, args, pMDImport, out, appendix);
        CorElementType underlyingType = ELEMENT_TYPE_MAX; // NOLINT(misc-const-correctness)
        typePtr += CorSigUncompressElementType(typePtr, &underlyingType);
        typePtr += CorSigUncompressToken(typePtr, &tk);

        std::list<std::string> genericArgs;

        unsigned numArgs = CorSigUncompressData(typePtr);
        while ((numArgs--) != 0U)
        {
            std::string genType;
            std::string genTypeAppendix;
            typePtr = NameForTypeSig(typePtr, args, pMDImport, genType, genTypeAppendix);
            genericArgs.push_back(genType + genTypeAppendix);
        }
        TypePrinter::NameForToken(tk, pMDImport, out, true, &genericArgs);
        break;
    }

    case ELEMENT_TYPE_PINNED:
        appendix = getGetNameWithAppendix(" pinned");
        break;
    case ELEMENT_TYPE_PTR:
        appendix = getGetNameWithAppendix("*");
        break;
    case ELEMENT_TYPE_BYREF:
        appendix = getGetNameWithAppendix("&");
        break;
    default:
    case ELEMENT_TYPE_SENTINEL:
    case ELEMENT_TYPE_END:
        // assert(!"Unknown Type");
        if (typ != 0)
        {
            out = "/* UNKNOWN TYPE (0x%X)*/" + std::to_string(typ);
        }
        break;
    } // end switch

    return typePtr;
}

} // unnamed namespace

// https://github.com/dotnet/runtime/blob/57bfe474518ab5b7cfe6bf7424a79ce3af9d6657/docs/design/coreclr/profiling/davbr-blog-archive/samples/sigparse.cpp
// This blog post originally appeared on David Broman's blog on 10/13/2005

// Sig ::= MethodDefSig | MethodRefSig | StandAloneMethodSig | FieldSig | PropertySig | LocalVarSig
// MethodDefSig ::= [[HASTHIS] [EXPLICITTHIS]] (DEFAULT|VARARG|GENERIC GenParamCount) ParamCount RetType Param*
// MethodRefSig ::= [[HASTHIS] [EXPLICITTHIS]] VARARG ParamCount RetType Param* [SENTINEL Param+]
// StandAloneMethodSig ::= [[HASTHIS] [EXPLICITTHIS]] (DEFAULT|VARARG|C|STDCALL|THISCALL|FASTCALL) ParamCount RetType
// Param* [SENTINEL Param+] FieldSig ::= FIELD CustomMod* Type PropertySig ::= PROPERTY [HASTHIS] ParamCount CustomMod*
// Type Param* LocalVarSig ::= LOCAL_SIG Count (TYPEDBYREF | ([CustomMod] [Constraint])* [BYREF] Type)+

// -------------

// CustomMod ::= ( CMOD_OPT | CMOD_REQD ) ( TypeDefEncoded | TypeRefEncoded )
// Constraint ::= #define ELEMENT_TYPE_PINNED
// Param ::= CustomMod* ( TYPEDBYREF | [BYREF] Type )
// RetType ::= CustomMod* ( VOID | TYPEDBYREF | [BYREF] Type )
// Type ::= ( BOOLEAN | CHAR | I1 | U1 | U2 | U2 | I4 | U4 | I8 | U8 | R4 | R8 | I | U |
// | VALUETYPE TypeDefOrRefEncoded
// | CLASS TypeDefOrRefEncoded
// | STRING
// | OBJECT
// | PTR CustomMod* VOID
// | PTR CustomMod* Type
// | FNPTR MethodDefSig
// | FNPTR MethodRefSig
// | ARRAY Type ArrayShape
// | SZARRAY CustomMod* Type
// | GENERICINST (CLASS | VALUETYPE) TypeDefOrRefEncoded GenArgCount Type*
// | VAR Number
// | MVAR Number

// ArrayShape ::= Rank NumSizes Size* NumLoBounds LoBound*

// TypeDefOrRefEncoded ::= TypeDefEncoded | TypeRefEncoded
// TypeDefEncoded ::= 32-bit-3-part-encoding-for-typedefs-and-typerefs
// TypeRefEncoded ::= 32-bit-3-part-encoding-for-typedefs-and-typerefs

// ParamCount ::= 29-bit-encoded-integer
// GenArgCount ::= 29-bit-encoded-integer
// Count ::= 29-bit-encoded-integer
// Rank ::= 29-bit-encoded-integer
// NumSizes ::= 29-bit-encoded-integer
// Size ::= 29-bit-encoded-integer
// NumLoBounds ::= 29-bit-encoded-integer
// LoBounds ::= 29-bit-encoded-integer
// Number ::= 29-bit-encoded-integer

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE *ppSig, SigElementType &sigElementType,
                         const std::vector<SigElementType> &typeGenerics,
                         const std::vector<SigElementType> &methodGenerics, bool addCorTypeName)
{
    HRESULT Status = S_OK;
    ULONG corType = 0;
    mdToken tk = mdTokenNil;
    *ppSig += CorSigUncompressData(*ppSig, &corType);
    sigElementType.corType = (CorElementType)corType;
    ULONG argNum = 0;

    switch (sigElementType.corType)
    {
    case ELEMENT_TYPE_VOID:
    case ELEMENT_TYPE_BOOLEAN:
    case ELEMENT_TYPE_CHAR:
    case ELEMENT_TYPE_I1:
    case ELEMENT_TYPE_U1:
    case ELEMENT_TYPE_I2:
    case ELEMENT_TYPE_U2:
    case ELEMENT_TYPE_I4:
    case ELEMENT_TYPE_U4:
    case ELEMENT_TYPE_I8:
    case ELEMENT_TYPE_U8:
    case ELEMENT_TYPE_R4:
    case ELEMENT_TYPE_R8:
    case ELEMENT_TYPE_STRING:
    case ELEMENT_TYPE_OBJECT:
        if (addCorTypeName)
        {
            GetCorTypeName(sigElementType.corType, sigElementType.typeName);
        }
        break;

    case ELEMENT_TYPE_VALUETYPE:
    case ELEMENT_TYPE_CLASS:
        *ppSig += CorSigUncompressToken(*ppSig, &tk);
        IfFailRet(TypePrinter::NameForTypeByToken(tk, pMDImport, sigElementType.typeName, nullptr));
        break;

    case ELEMENT_TYPE_SZARRAY:
        if (FAILED(Status = ParseElementType(pMDImport, ppSig, sigElementType, typeGenerics, methodGenerics, true)) || Status == S_FALSE)
        {
            return Status;
        }
        sigElementType.corType = (CorElementType)corType;
        sigElementType.typeName += "[]";
        break;
    case ELEMENT_TYPE_ARRAY:
    {
        if (FAILED(Status = ParseElementType(pMDImport, ppSig, sigElementType, typeGenerics, methodGenerics, true)) || Status == S_FALSE)
        {
            return Status;
        }
        sigElementType.corType = (CorElementType)corType;
        // Parse for the rank
        ULONG rank = 0;
        *ppSig += CorSigUncompressData(*ppSig, &rank);
        // if rank == 0, we are done
        if (rank == 0)
        {
            break;
        }
        // any size of dimension specified?
        ULONG sizeDim = 0;
        ULONG ulTemp = 0;
        *ppSig += CorSigUncompressData(*ppSig, &sizeDim);
        while ((sizeDim--) != 0U)
        {
            *ppSig += CorSigUncompressData(*ppSig, &ulTemp);
        }
        // any lower bound specified?
        ULONG lowerBound = 0;
        int iTemp = 0;
        *ppSig += CorSigUncompressData(*ppSig, &lowerBound);
        while ((lowerBound--) != 0U)
        {
            *ppSig += CorSigUncompressSignedInt(*ppSig, &iTemp);
        }
        sigElementType.typeName += "[" + std::string(rank - 1, ',') + "]";
        break;
    }

    case ELEMENT_TYPE_VAR: // Generic parameter in a generic type definition, represented as number
        *ppSig += CorSigUncompressData(*ppSig, &argNum);
        if (argNum >= typeGenerics.size())
        {
            return S_FALSE;
        }
        else
        {
            sigElementType = typeGenerics[argNum];
            if (addCorTypeName && sigElementType.typeName.empty())
            {
                GetCorTypeName(sigElementType.corType, sigElementType.typeName);
            }
        }
        break;

    case ELEMENT_TYPE_MVAR: // Generic parameter in a generic method definition, represented as number
        *ppSig += CorSigUncompressData(*ppSig, &argNum);
        if (argNum >= methodGenerics.size())
        {
            return S_FALSE;
        }
        else
        {
            sigElementType = methodGenerics[argNum];
            if (addCorTypeName && sigElementType.typeName.empty())
            {
                GetCorTypeName(sigElementType.corType, sigElementType.typeName);
            }
        }
        break;

    case ELEMENT_TYPE_GENERICINST: // A type modifier for generic types - List<>, Dictionary<>, ...
    {
        ULONG number = 0;
        mdToken token = mdTokenNil;
        *ppSig += CorSigUncompressData(*ppSig, &corType);
        if (corType != ELEMENT_TYPE_CLASS && corType != ELEMENT_TYPE_VALUETYPE)
        {
            return S_FALSE;
        }
        *ppSig += CorSigUncompressToken(*ppSig, &token);
        sigElementType.corType = (CorElementType)corType;
        IfFailRet(TypePrinter::NameForTypeByToken(token, pMDImport, sigElementType.typeName, nullptr));
        *ppSig += CorSigUncompressData(*ppSig, &number);
        for (ULONG i = 0; i < number; i++)
        {
            SigElementType mycop; // Not needed at the moment
            if (FAILED(Status = ParseElementType(pMDImport, ppSig, mycop, typeGenerics, methodGenerics, true)) || Status == S_FALSE)
            {
                return Status;
            }
        }
        break;
    }

        // TODO
    case ELEMENT_TYPE_U: // "nuint" - error CS8652: The feature 'native-sized integers' is currently in Preview and
                         // *unsupported*. To use Preview features, use the 'preview' language version.
    case ELEMENT_TYPE_I: // "nint" - error CS8652: The feature 'native-sized integers' is currently in Preview and
                         // *unsupported*. To use Preview features, use the 'preview' language version.
    case ELEMENT_TYPE_TYPEDBYREF:
    case ELEMENT_TYPE_PTR:   // int* ptr (unsafe code only)
    case ELEMENT_TYPE_BYREF: // ref, in, out
    case ELEMENT_TYPE_CMOD_REQD:
    case ELEMENT_TYPE_CMOD_OPT:
        return S_FALSE;

    default:
        return E_INVALIDARG;
    }

    return S_OK;
}

// Return S_FALSE in case abort parsing, since next block are not implemented.
HRESULT SigParse(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, const std::vector<SigElementType> &typeGenerics,
                 const std::vector<SigElementType> &methodGenerics, SigElementType &returnElementType,
                 std::vector<SigElementType> &argElementTypes, bool addCorTypeName)
{
    HRESULT Status = S_OK;
    ULONG gParams = 0; // Count of signature generics
    ULONG cParams = 0; // Count of signature parameters.
    ULONG elementSize = 0;
    ULONG convFlags = 0;

    returnElementType.corType = ELEMENT_TYPE_MAX;
    returnElementType.typeName.clear();
    argElementTypes.clear();

    // 1. calling convention for MethodDefSig:
    // [[HASTHIS] [EXPLICITTHIS]] (DEFAULT|VARARG|GENERIC GenParamCount)
    elementSize = CorSigUncompressData(pSig, &convFlags);
    pSig += elementSize;

    // TODO add VARARG methods support.
    if ((convFlags & SIG_METHOD_VARARG) != 0U)
    {
        return S_FALSE;
    }

    // 2. count of generics if any
    if ((convFlags & SIG_METHOD_GENERIC) != 0U)
    {
        elementSize = CorSigUncompressData(pSig, &gParams);
        pSig += elementSize;
    }

    // 3. count of params
    elementSize = CorSigUncompressData(pSig, &cParams);
    pSig += elementSize;

    // 4. return type
    IfFailRet(ParseElementType(pMDImport, &pSig, returnElementType, typeGenerics, methodGenerics, addCorTypeName));
    if (Status == S_FALSE)
    {
        return S_FALSE;
    }

    // 5. get next element from method signature
    argElementTypes.resize(cParams);
    for (ULONG i = 0; i < cParams; ++i)
    {
        IfFailRet(ParseElementType(pMDImport, &pSig, argElementTypes[i], typeGenerics, methodGenerics, addCorTypeName));
        if (Status == S_FALSE)
        {
            return S_FALSE;
        }
    }

    return S_OK;
}

void NameForTypeSig(PCCOR_SIGNATURE typePtr, ICorDebugType *pEnclosingType, IMetaDataImport *pMDImport, std::string &typeName)
{
    // Gather generic arguments from enclosing type
    std::vector<std::string> args;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;

    if (SUCCEEDED(pEnclosingType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG fetched = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
        {
            std::string name;
            TypePrinter::GetTypeOfValue(trCurrentTypeParam, name);
            args.emplace_back(name);
            trCurrentTypeParam.Free();
        }
    }

    std::string out;
    std::string appendix;
    NameForTypeSig(typePtr, args, pMDImport, out, appendix);
    typeName = out + appendix;
}

} // namespace dncdbg
