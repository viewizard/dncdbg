// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/sigparse.h"
#include "metadata/corhelpers.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include <cassert>
#include <unordered_map>

namespace dncdbg
{

namespace
{

// https://github.com/dotnet/runtime/blob/57bfe474518ab5b7cfe6bf7424a79ce3af9d6657/docs/design/coreclr/profiling/davbr-blog-archive/samples/sigparse.cpp
constexpr ULONG SIG_METHOD_VARARG = 0x5;   // vararg calling convention
constexpr ULONG SIG_METHOD_GENERIC = 0x10; // used to indicate that the method has one or more generic parameters.

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
        while (sizeDim != 0U)
        {
            sizeDim--;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, ulTemp));
        }
        ULONG lowerBound = 0;
        int iTemp = 0;
        IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, lowerBound));
        while (lowerBound != 0U)
        {
            lowerBound--;
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

        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, elemType));

        switch (elemType)
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
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
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
            CorElementType innerElemType = ELEMENT_TYPE_MAX;
            IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, innerElemType));
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
            ULONG number = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, number));
            assert(number <= static_cast<ULONG>(std::numeric_limits<int>::max()));
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

// Array wrapper (SZARRAY/ARRAY) that modifies a type in the signature.
// Each entry: { outerElemType, suffix }, where the suffix is appended after the
// wrapped type is completely resolved.
struct TypeWrapper
{
    CorElementType outerElemType;
    std::string suffix;
};

// Append the collected array suffixes to `name`.
// Wrappers are collected outermost-first (so the innermost wrapper is pushed
// last), and the array shape data in the signature appears right after the
// wrapped type (innermost shape first), so reverse iteration matches the
// signature layout.
HRESULT ApplyTypeWrappers(std::vector<TypeWrapper> &wrappers, PCCOR_SIGNATURE &pSig,
                          PCCOR_SIGNATURE pSigEnd, std::string &name)
{
    HRESULT Status = S_OK;

    for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it)
    {
        if (it->outerElemType == ELEMENT_TYPE_ARRAY)
        {
            // Read the rank from a copy of the pointer, then skip the full shape.
            PCCOR_SIGNATURE rankPtr = pSig;
            ULONG rank = 0;
            IfFailRet(CorSigUncompressData_EndPtr(rankPtr, pSigEnd, rank));
            IfFailRet(SkipArrayShape(pSig, pSigEnd));
            if (rank != 0)
            {
                it->suffix = "[" + std::string(rank - 1, ',') + "]";
            }
        }
        name += it->suffix;
    }

    return S_OK;
}

// Parse one `Type` from the signature blob and build its "display" name, for example
// "System.Collections.Generic.List<System.Collections.Generic.List<int>>".
//
// ELEMENT_TYPE_GENERICINST could be used multiple times in the same signature blob
// (nested generic type parameters like List<List<List<int>>>), so this method must not
// rely on recursion. Instead, an explicit stack of "frames" is used, where each frame
// represents one generic instantiation with generic arguments that are not parsed yet.
// Since the signature stores types in prefix order (generic type first, then its generic
// arguments), a frame could be finalized (its "<...>" argument list filled in) as soon as
// all its generic arguments are resolved, and the resulting string is handed over to the
// enclosing frame as one of its generic arguments (or returned as the final result for the
// outermost/root type).
HRESULT ParseGenTypeDisplayName(IMetaDataImport *pMDImport, PCCOR_SIGNATURE &pSig,
                                PCCOR_SIGNATURE pSigEnd, std::string &displayName)
{
    HRESULT Status = S_OK;
    displayName.clear();

    // One pending generic instantiation (ELEMENT_TYPE_GENERICINST).
    struct GenericInstFrame
    {
        std::string baseMetadataName;      // "metadata" name of the generic type, for example "...List`1"
        ULONG argsLeft{0};                 // count of generic arguments that are not parsed yet
        std::list<std::string> args;       // "display" names of already parsed generic arguments
        std::vector<TypeWrapper> wrappers; // array suffixes applied to the whole generic type
    };
    std::vector<GenericInstFrame> frames;

    bool rootDone = false; // the outermost (root) type is completely resolved

    // Store a completely resolved type name as the next generic argument of the innermost
    // pending generic instantiation, or as the final result in case of the root type.
    const auto deliverTypeName = [&frames, &displayName, &rootDone](std::string &&name)
    {
        if (frames.empty())
        {
            displayName = std::move(name);
            rootDone = true;
            return;
        }

        GenericInstFrame &parent = frames.back();
        assert(parent.argsLeft != 0);
        parent.args.emplace_back(std::move(name));
        parent.argsLeft--;
    };

    // Note, each iteration either consumes signature data or pops one frame, so the loop
    // is bounded by the signature blob size (pSigEnd) and cannot spin forever.
    while (!rootDone)
    {
        // 1. Finalize the innermost generic instantiation with all generic arguments resolved.
        if (!frames.empty() && frames.back().argsLeft == 0)
        {
            GenericInstFrame frame = std::move(frames.back());
            frames.pop_back();
            // Note, ConvertMetadataToDisplayName() consumes `args` in order to fill the
            // "<...>" argument list(s) and converts "metadata" nested type delimiters ('+')
            // into "display" ones ('.').
            std::string name = MetadataHelpers::ConvertMetadataToDisplayName(frame.baseMetadataName, &frame.args);
            // The array shape data follows the whole wrapped type in the signature; this is
            // why it must be processed only after all generic arguments have been parsed.
            IfFailRet(ApplyTypeWrappers(frame.wrappers, pSig, pSigEnd, name));
            deliverTypeName(std::move(name));
            continue;
        }

        // 2. Parse next type from the signature - the root type at the very first iteration,
        //    the next generic argument of the innermost pending generic instantiation after that.
        std::vector<TypeWrapper> wrappers;
        std::string typeName;
        bool genericInstPushed = false;

        // Peel off wrapping element types (SZARRAY, ARRAY) that modify the inner type.
        for (;;)
        {
            CorElementType elemType = ELEMENT_TYPE_MAX;
            IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, elemType));

            if (elemType == ELEMENT_TYPE_SZARRAY)
            {
                // Record the "[]" suffix and continue the loop to parse the inner type.
                wrappers.push_back({elemType, "[]"});
                continue;
            }

            if (elemType == ELEMENT_TYPE_ARRAY)
            {
                // The array shape data follows the inner type in the signature, so we
                // parse the inner type first (by continuing the loop) and fill in the
                // suffix from the shape data later.
                wrappers.push_back({elemType, {}});
                continue;
            }

            switch (elemType)
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
            case ELEMENT_TYPE_U:
            case ELEMENT_TYPE_I:
            case ELEMENT_TYPE_STRING:
            case ELEMENT_TYPE_OBJECT:
                Status = MetadataHelpers::GetBuiltInTypeName(elemType, typeName);
                assert(SUCCEEDED(Status));
                break;

            case ELEMENT_TYPE_VALUETYPE:
            case ELEMENT_TYPE_CLASS:
            {
                mdToken token = mdTokenNil;
                IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
                std::string metadataName;
                IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(token, pMDImport, metadataName));
                // Note, this is a non-generic type here (generic types are always represented
                // by ELEMENT_TYPE_GENERICINST in the signature); the conversion is needed only
                // for nested type delimiters ('+' in "metadata" name, '.' in "display" name).
                typeName = MetadataHelpers::ConvertMetadataToDisplayName(metadataName, nullptr);
                break;
            }

            case ELEMENT_TYPE_GENERICINST: // A type modifier for generic types - List<>, Dictionary<>, ...
            {
                CorElementType innerElemType = ELEMENT_TYPE_MAX;
                IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, innerElemType));
                if (innerElemType != ELEMENT_TYPE_CLASS &&
                    innerElemType != ELEMENT_TYPE_VALUETYPE)
                {
                    return E_NOTIMPL;
                }
                mdToken token = mdTokenNil;
                IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));

                GenericInstFrame frame;
                IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(token, pMDImport, frame.baseMetadataName));
                IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, frame.argsLeft));
                frame.wrappers = std::move(wrappers);
                frames.emplace_back(std::move(frame));
                genericInstPushed = true;
                break;
            }

            case ELEMENT_TYPE_VAR: // Generic parameter in a generic type definition, represented as number
            {
                ULONG num = 0;
                IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, num));
                // FIXME: produces the type name List<!1> (generic parameter not resolved)
                typeName = "!" + std::to_string(num);
                break;
            }

            case ELEMENT_TYPE_MVAR: // Generic parameter in a generic method definition, represented as number
            {
                ULONG num = 0;
                IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, num));
                // FIXME: produces the type name List<!!1> (generic parameter not resolved)
                typeName = "!!" + std::to_string(num);
                break;
            }

            default:
                return E_INVALIDARG;
            }

            // Base type resolved, exit the peeling loop.
            break;
        }

        if (genericInstPushed)
        {
            // Generic arguments of this generic instantiation (which could be generic
            // instantiations too) will be parsed by subsequent loop iterations.
            continue;
        }

        IfFailRet(ApplyTypeWrappers(wrappers, pSig, pSigEnd, typeName));
        deliverTypeName(std::move(typeName));
    }

    return S_OK;
}

} // unnamed namespace

bool SigElementType::isAlias(const CorElementType elemType1, const CorElementType elemType2, const std::string &name2)
{
    static const std::unordered_map<CorElementType, SigElementType> aliases{
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

    const auto found = aliases.find(elemType1);
    if (found != aliases.cend())
    {
        if (found->second.elemType == elemType2 && found->second.metadataTypeName == name2)
        {
            return true;
        }
    }
    return false;
}

bool SigElementType::areEqual(const SigElementType &arg) const
{
    return (elemType == arg.elemType && metadataTypeName == arg.metadataTypeName &&
            // in case of generic type, must be applied to real type first
            genericElemType == ELEMENT_TYPE_END && arg.genericElemType == ELEMENT_TYPE_END) ||
           isAlias(elemType, arg.elemType, arg.metadataTypeName) ||
           isAlias(arg.elemType, elemType, metadataTypeName);
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
                         DWORD flags, SigElementType &sigElementType, std::list<std::string> *args,
                         bool addElementTypeName)
{
    HRESULT Status = S_OK;

    // Collect array/wrapper suffixes iteratively instead of recursing.
    std::vector<TypeWrapper> wrappers;

    // Peel off wrapping element types (SZARRAY, ARRAY, BYREF) that modify the inner
    // type, then post-process the result.
    for (;;)
    {
        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, elemType));
        sigElementType.elemType = elemType;

        if (elemType == ELEMENT_TYPE_SZARRAY)
        {
            // Record the "[]" suffix and continue the loop to parse the inner type.
            wrappers.push_back({elemType, "[]"});
            // addElementTypeName must be true for inner types (matches original recursive call).
            addElementTypeName = true;
            continue;
        }

        if (elemType == ELEMENT_TYPE_ARRAY)
        {
            // The array shape data follows the inner type in the signature, so we
            // parse the inner type first (by continuing the loop) and fill in the
            // suffix from the shape data after the loop.
            wrappers.push_back({elemType, {}});
            addElementTypeName = true;
            continue;
        }

        if (elemType == ELEMENT_TYPE_BYREF)
        {
            // In .NET metadata: 'out' has pdOut only, 'in' has pdIn only,
            // 'ref' has both pdIn and pdOut, or neither flag set.
            if ((flags & pdOut) != 0U && (flags & pdIn) == 0U)
            {
                sigElementType.parameterModifier = "out";
            }
            else if ((flags & pdIn) != 0U && (flags & pdOut) == 0U)
            {
                sigElementType.parameterModifier = "in";
            }
            else
            {
                sigElementType.parameterModifier = "ref";
            }

            addElementTypeName = true;
            continue;
        }

        switch (sigElementType.elemType)
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
        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_OBJECT:
            if (addElementTypeName)
            {
                Status = MetadataHelpers::GetBuiltInTypeName(sigElementType.elemType, sigElementType.metadataTypeName);
                assert(SUCCEEDED(Status));
            }
            break;

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
            IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(token, pMDImport, sigElementType.metadataTypeName));
            break;
        }

        case ELEMENT_TYPE_VAR: // Generic parameter in a generic type definition, represented as number
        {
            ULONG num = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, num));
            sigElementType.genericElemType = ELEMENT_TYPE_VAR;
            sigElementType.varNum = num;
            break;
        }

        case ELEMENT_TYPE_MVAR: // Generic parameter in a generic method definition, represented as number
        {
            ULONG num = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, num));
            sigElementType.genericElemType = ELEMENT_TYPE_MVAR;
            sigElementType.varNum = num;
            break;
        }

        case ELEMENT_TYPE_GENERICINST: // A type modifier for generic types - List<>, Dictionary<>, ...
        {
            CorElementType innerElemType = ELEMENT_TYPE_MAX;
            IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, innerElemType));
            if (innerElemType != ELEMENT_TYPE_CLASS &&
                innerElemType != ELEMENT_TYPE_VALUETYPE)
            {
                return E_NOTIMPL;
            }
            mdToken token = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, token));
            sigElementType.elemType = innerElemType;
            IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(token, pMDImport, sigElementType.metadataTypeName));
            ULONG number = 0;
            IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, number));
            for (ULONG i = 0; i < number; i++)
            {
                if (args == nullptr)
                {
                    IfFailRet(SkipElementType(pSig, pSigEnd));
                }
                else
                {
                    // Note, ParseGenTypeDisplayName() supports nested generic type parameters
                    // (for example, List<List<List<int>>>) and provides a ready-to-use "display" name.
                    std::string displayName;
                    IfFailRet(ParseGenTypeDisplayName(pMDImport, pSig, pSigEnd, displayName));
                    args->emplace_back(std::move(displayName));
                }
            }
            break;
        }

        // TODO
        case ELEMENT_TYPE_TYPEDBYREF:
        case ELEMENT_TYPE_PTR:   // int* ptr (unsafe code only)
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_CMOD_OPT:
            return E_NOTIMPL;

        default:
            return E_INVALIDARG;
        }

        // Base type resolved, exit the peeling loop.
        break;
    }

    // Apply the collected array suffixes ("[]", "[,]", ...) to the resolved type name.
    IfFailRet(ApplyTypeWrappers(wrappers, pSig, pSigEnd, sigElementType.metadataTypeName));

    // Set the outermost elemType if there were any wrappers.
    if (!wrappers.empty())
    {
        sigElementType.elemType = wrappers.front().outerElemType;
    }

    return S_OK;
}

HRESULT ParseMethodSig(IMetaDataImport *pMDImport, mdMethodDef methodDef, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                       SigElementType &returnElementType, std::vector<SigElementType> &argElementTypes,
                       bool addElementTypeName, uint32_t *methodGenParamCount)
{
    HRESULT Status = S_OK;
    ULONG gParams = 0; // Count of signature generics
    ULONG cParams = 0; // Count of signature parameters.
    ULONG convFlags = 0;

    returnElementType.Clear();
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
        if (methodGenParamCount != nullptr)
        {
            *methodGenParamCount = static_cast<uint32_t>(gParams);
        }
    }

    // 3. count of params
    IfFailRet(CorSigUncompressData_EndPtr(pSig, pSigEnd, cParams));

    // 4. return type
    IfFailRet(ParseElementType(pMDImport, pSig, pSigEnd, 0, returnElementType, nullptr, addElementTypeName));

    // 5. get next element from method signature
    argElementTypes.resize(cParams);
    for (ULONG i = 0; i < cParams; ++i)
    {
        // The Param table is optional — not every parameter has a Param row.
        // If GetParamForMethodIndex fails, default to flags = 0 (plain 'ref').
        mdParamDef paramDef = mdParamDefNil;
        DWORD flags = 0;
        if (SUCCEEDED(pMDImport->GetParamForMethodIndex(methodDef, i + 1, &paramDef)) &&
            paramDef != mdParamDefNil)
        {
            // Ignore GetParamProps failure — flags will remain 0.
            pMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0, nullptr,
                                     &flags, nullptr, nullptr, nullptr);
        }
        IfFailRet(ParseElementType(pMDImport, pSig, pSigEnd, flags, argElementTypes.at(i), nullptr, addElementTypeName));
    }

    return S_OK;
}

HRESULT ApplyGenericTypeParameters(const std::vector<SigElementType> &genericTypeParameters, SigElementType &methodArg)
{
    if (methodArg.genericElemType == ELEMENT_TYPE_VAR)
    {
        if (methodArg.varNum >= genericTypeParameters.size())
        {
            return E_INVALIDARG;
        }
        methodArg.elemType = genericTypeParameters.at(methodArg.varNum).elemType;
        methodArg.metadataTypeName = genericTypeParameters.at(methodArg.varNum).metadataTypeName;
        methodArg.genericElemType = ELEMENT_TYPE_END;
        methodArg.varNum = 0;
    }

    return S_OK;
}

HRESULT ApplyGenericMethodParameters(const std::vector<SigElementType> &genericMethodParameters, SigElementType &methodArg)
{
    if (methodArg.genericElemType == ELEMENT_TYPE_MVAR)
    {
        if (methodArg.varNum >= genericMethodParameters.size())
        {
            return E_INVALIDARG;
        }
        methodArg.elemType = genericMethodParameters.at(methodArg.varNum).elemType;
        methodArg.metadataTypeName = genericMethodParameters.at(methodArg.varNum).metadataTypeName;
        methodArg.genericElemType = ELEMENT_TYPE_END;
        methodArg.varNum = 0;
    }

    return S_OK;
}

} // namespace dncdbg
