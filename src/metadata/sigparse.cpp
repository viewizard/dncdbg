// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/sigparse.h"
#include "metadata/corhelpers.h"
#include "metadata/typeprinter.h"
#include "utils/torelease.h"
#include <unordered_map>

namespace dncdbg
{

namespace
{

// https://github.com/dotnet/runtime/blob/57bfe474518ab5b7cfe6bf7424a79ce3af9d6657/docs/design/coreclr/profiling/davbr-blog-archive/samples/sigparse.cpp
constexpr ULONG SIG_METHOD_VARARG = 0x5;   // vararg calling convention
constexpr ULONG SIG_METHOD_GENERIC = 0x10; // used to indicate that the method has one or more generic parameters.

void GetCorTypeName(CorElementType corType, std::string &typeName)
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

// Skip array shape data in the signature (rank, sizes, lower bounds).
HRESULT SkipArrayShape(PCCOR_SIGNATURE &pSig, PCCOR_SIGNATURE pSigEnd)
{
    HRESULT Status = S_OK;
    ULONG rank = 0;
    IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, rank));
    if (rank != 0)
    {
        ULONG sizeDim = 0;
        ULONG ulTemp = 0;
        IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, sizeDim));
        while ((sizeDim--) != 0U)
        {
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, ulTemp));
        }
        ULONG lowerBound = 0;
        int iTemp = 0;
        IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, lowerBound));
        while ((lowerBound--) != 0U)
        {
            IfFailRet(CorSigUncompressSignedInt_EndPtr(pSig, pSigEnd, iTemp));
        }
    }

    return S_OK;
}

// Advance the signature pointer past one element type without building a result.
// This is used to skip generic arguments in GENERICINST that are not needed.
HRESULT SkipElementType(PCCOR_SIGNATURE &pSig, PCCOR_SIGNATURE pSigEnd)
{
    HRESULT Status = S_OK;
    // Work items: positive value N means "skip N element types";
    // negative value -1 means "skip array shape data".
    constexpr int SKIP_ARRAY_SHAPE = -1;
    std::vector<int> work;
    work.push_back(1); // skip one element type

    while (!work.empty())
    {
        int &top = work.back();
        if (top == SKIP_ARRAY_SHAPE)
        {
            work.pop_back();
            IfFailRet(SkipArrayShape(pSig, pSigEnd));
            continue;
        }
        if (top <= 0)
        {
            work.pop_back();
            continue;
        }
        --top;

        CorElementType corType = ELEMENT_TYPE_MAX;
        IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, corType));

        switch (corType)
        {
        // Primitive types — no additional data to skip.
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
        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_TYPEDBYREF:
            break;

        // Token-based types — skip the compressed token.
        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            mdToken tk = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, tk));
            break;
        }

        // Generic parameter references — skip the compressed number.
        case ELEMENT_TYPE_VAR:
        case ELEMENT_TYPE_MVAR:
        {
            ULONG num = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, num));
            break;
        }

        // Single-dimensional zero-based array — wraps one inner type.
        case ELEMENT_TYPE_SZARRAY:
            work.push_back(1); // skip the inner element type
            break;

        // Multi-dimensional array — wraps one inner type, followed by array shape.
        case ELEMENT_TYPE_ARRAY:
            // Push array shape skip first (will be processed after inner type).
            work.push_back(SKIP_ARRAY_SHAPE);
            // Then push inner element type skip (will be processed first — LIFO).
            work.push_back(1);
            break;

        // Generic instantiation — skip underlying type token + N generic arguments.
        case ELEMENT_TYPE_GENERICINST:
        {
            CorElementType innerCorType = ELEMENT_TYPE_MAX;
            IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, innerCorType));
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
            ULONG number = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, number));
            work.push_back(static_cast<int>(number)); // skip N generic arg element types
            break;
        }

        // Modifier types that wrap one inner type.
        case ELEMENT_TYPE_PTR:
        case ELEMENT_TYPE_BYREF:
        case ELEMENT_TYPE_PINNED:
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_CMOD_OPT:
            work.push_back(1);
            break;

        default:
            break;
        }
    }

    return S_OK;
}

} // unnamed namespace

bool SigElementType::isAlias(const CorElementType type1, const CorElementType type2, const std::string &name2)
{
    static const std::unordered_map<CorElementType, SigElementType> aliases = {
        {ELEMENT_TYPE_BOOLEAN, {ELEMENT_TYPE_VALUETYPE, "System.Boolean"}},
        {ELEMENT_TYPE_CHAR,    {ELEMENT_TYPE_VALUETYPE, "System.Char"}},
        {ELEMENT_TYPE_I1,      {ELEMENT_TYPE_VALUETYPE, "System.Byte"}},
        {ELEMENT_TYPE_U1,      {ELEMENT_TYPE_VALUETYPE, "System.SByte"}},
        {ELEMENT_TYPE_R8,      {ELEMENT_TYPE_VALUETYPE, "System.Double"}},
        {ELEMENT_TYPE_R4,      {ELEMENT_TYPE_VALUETYPE, "System.Single"}},
        {ELEMENT_TYPE_I4,      {ELEMENT_TYPE_VALUETYPE, "System.Int32"}},
        {ELEMENT_TYPE_U4,      {ELEMENT_TYPE_VALUETYPE, "System.UInt32"}},
        {ELEMENT_TYPE_I8,      {ELEMENT_TYPE_VALUETYPE, "System.Int64"}},
        {ELEMENT_TYPE_U8,      {ELEMENT_TYPE_VALUETYPE, "System.UInt64"}},
        {ELEMENT_TYPE_OBJECT,  {ELEMENT_TYPE_CLASS,     "System.Object"}},
        {ELEMENT_TYPE_I2,      {ELEMENT_TYPE_VALUETYPE, "System.Int16"}},
        {ELEMENT_TYPE_U2,      {ELEMENT_TYPE_VALUETYPE, "System.UInt16"}},
        {ELEMENT_TYPE_STRING,  {ELEMENT_TYPE_CLASS,     "System.String"}}
    };

    auto found = aliases.find(type1);
    if (found != aliases.end())
    {
        if (found->second.corType == type2 && found->second.typeName == name2)
        {
            return true;
        }
    }
    return false;
}

bool SigElementType::areEqual(const SigElementType &arg) const
{
    return (corType == arg.corType && typeName == arg.typeName &&
            // in case of generic type, must be applied to real type first
            elementType == ELEMENT_TYPE_END && arg.elementType == ELEMENT_TYPE_END) ||
           isAlias(corType, arg.corType, arg.typeName) ||
           isAlias(arg.corType, corType, typeName);
}

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

HRESULT ParseElementType(IMetaDataImport *pMDImport, PCCOR_SIGNATURE &pSig, PCCOR_SIGNATURE pSigEnd,
                         SigElementType &sigElementType, bool addCorTypeName)
{
    HRESULT Status = S_OK;

    // Collect array/wrapper suffixes iteratively instead of recursing.
    // Each entry: { outerCorType, suffix } where suffix is appended after the base type is resolved.
    struct Wrapper
    {
        CorElementType outerCorType;
        std::string suffix;
    };
    std::vector<Wrapper> wrappers;

    // Peel off wrapping element types (SZARRAY, ARRAY) that would otherwise recurse
    // to parse their inner element type, then post-process the result.
    for (;;)
    {
        CorElementType corType = ELEMENT_TYPE_MAX;
        IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, corType));
        sigElementType.corType = corType;

        if (corType == ELEMENT_TYPE_SZARRAY)
        {
            // The recursive version parsed the inner type first, then set corType back to SZARRAY
            // and appended "[]". We record this and continue the loop to parse the inner type.
            wrappers.push_back({corType, "[]"});
            // addCorTypeName must be true for inner types (matches original recursive call).
            addCorTypeName = true;
            continue;
        }

        if (corType == ELEMENT_TYPE_ARRAY)
        {
            // We need to read the inner type first (handled by continuing the loop),
            // but the array shape data follows the inner type in the signature.
            // Record a placeholder suffix; we'll fill it in after the loop.
            wrappers.push_back({corType, {}});
            addCorTypeName = true;
            continue;
        }

        // Not a wrapping type — handle the base element type.
        ULONG argNum = 0;
        mdToken tk = mdTokenNil;

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
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, tk));
            IfFailRet(TypePrinter::NameForTypeByToken(tk, pMDImport, sigElementType.typeName, nullptr));
            break;

        case ELEMENT_TYPE_VAR: // Generic parameter in a generic type definition, represented as number
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, argNum));
            sigElementType.elementType = ELEMENT_TYPE_VAR;
            sigElementType.varNum = argNum;
            break;

        case ELEMENT_TYPE_MVAR: // Generic parameter in a generic method definition, represented as number
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, argNum));
            sigElementType.elementType = ELEMENT_TYPE_MVAR;
            sigElementType.varNum = argNum;
            break;

        case ELEMENT_TYPE_GENERICINST: // A type modifier for generic types - List<>, Dictionary<>, ...
        {
            CorElementType innerCorType = ELEMENT_TYPE_MAX;
            IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, innerCorType));
            if (innerCorType != ELEMENT_TYPE_CLASS &&
                innerCorType != ELEMENT_TYPE_VALUETYPE)
            {
                return E_NOTIMPL;
            }
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
            sigElementType.corType = innerCorType;
            IfFailRet(TypePrinter::NameForTypeByToken(token, pMDImport, sigElementType.typeName, nullptr));
            ULONG number = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, number));
            for (ULONG i = 0; i < number; i++)
            {
                IfFailRet(SkipElementType(pSig, pSigEnd)); // Not needed at the moment, just advance past each generic arg
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
            return E_NOTIMPL;

        default:
            return E_INVALIDARG;
        }

        // Base type resolved, exit the peeling loop.
        break;
    }

    // Apply the collected wrappers in reverse order (innermost wrapper was pushed last).
    // Wrappers are collected outermost-first, and the array shape data in the signature
    // appears right after the inner type. We process wrappers in reverse order to match
    // the signature layout.
    for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it)
    {
        if (it->outerCorType == ELEMENT_TYPE_ARRAY)
        {
            // Parse array shape from the signature and build the suffix.
            // Save position to read rank before skipping.
            PCCOR_SIGNATURE rankPtr = pSig;
            ULONG rank = 0;
            IfFailRet(CorSigUncompressData_EndPtr(rankPtr, pSigEnd, rank));
            // Skip the entire array shape data.
            IfFailRet(SkipArrayShape(pSig, pSigEnd));
            if (rank != 0)
            {
                it->suffix = "[" + std::string(rank - 1, ',') + "]";
            }
        }
        sigElementType.typeName += it->suffix;
    }

    // Set the outermost corType if there were any wrappers.
    if (!wrappers.empty())
    {
        sigElementType.corType = wrappers.front().outerCorType;
    }

    return S_OK;
}

HRESULT ParseMethodSig(IMetaDataImport *pMDImport, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, SigElementType &returnElementType,
                       std::vector<SigElementType> &argElementTypes, bool addCorTypeName)
{
    HRESULT Status = S_OK;
    ULONG gParams = 0; // Count of signature generics
    ULONG cParams = 0; // Count of signature parameters.
    ULONG convFlags = 0;

    returnElementType.corType = ELEMENT_TYPE_MAX;
    returnElementType.typeName.clear();
    argElementTypes.clear();

    // 1. calling convention for MethodDefSig:
    // [[HASTHIS] [EXPLICITTHIS]] (DEFAULT|VARARG|GENERIC GenParamCount)
    IfFailRet(CorSigUncompressCallingConv_EndPtr(pSig, pSigEnd, convFlags));

    // TODO add VARARG methods support.
    if ((convFlags & SIG_METHOD_VARARG) != 0U)
    {
        return E_NOTIMPL;
    }

    // 2. count of generics if any
    if ((convFlags & SIG_METHOD_GENERIC) != 0U)
    {
        IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, gParams));
    }

    // 3. count of params
    IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, cParams));

    // 4. return type
    IfFailRet(ParseElementType(pMDImport, pSig, pSigEnd, returnElementType, addCorTypeName));

    // 5. get next element from method signature
    argElementTypes.resize(cParams);
    for (ULONG i = 0; i < cParams; ++i)
    {
        IfFailRet(ParseElementType(pMDImport, pSig, pSigEnd, argElementTypes.at(i), addCorTypeName));
    }

    return S_OK;
}

HRESULT ApplyTypeGenerics(const std::vector<SigElementType> &typeGenerics, SigElementType &methodArg)
{
    if (methodArg.elementType == ELEMENT_TYPE_VAR)
    {
        if (methodArg.varNum >= typeGenerics.size())
        {
            return E_INVALIDARG;
        }
        methodArg.corType = typeGenerics.at(methodArg.varNum).corType;
        methodArg.typeName = typeGenerics.at(methodArg.varNum).typeName;
        methodArg.elementType = ELEMENT_TYPE_END;
        methodArg.varNum = 0;
    }

    return S_OK;
}

HRESULT ApplyMethodGenerics(const std::vector<SigElementType> &methodGenerics, SigElementType &methodArg)
{
    if (methodArg.elementType == ELEMENT_TYPE_MVAR)
    {
        if (methodArg.varNum >= methodGenerics.size())
        {
            return E_INVALIDARG;
        }
        methodArg.corType = methodGenerics.at(methodArg.varNum).corType;
        methodArg.typeName = methodGenerics.at(methodArg.varNum).typeName;
        methodArg.elementType = ELEMENT_TYPE_END;
        methodArg.varNum = 0;
    }

    return S_OK;
}

} // namespace dncdbg
