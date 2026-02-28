// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluator.h"
#include "debugger/evalhelpers.h"
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalutils.h"
#include "debugger/frames.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h"
#include "managed/interop.h"
#include "metadata/attributes.h"
#include "metadata/sigparse.h"
#include "metadata/typeprinter.h"
#include "utils/utf.h"
#include <array>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace dncdbg
{

namespace
{

void IncIndicies(std::vector<uint32_t> &ind, const std::vector<uint32_t> &dims)
{
    int i = static_cast<int32_t>(ind.size()) - 1;

    while (i >= 0)
    {
        ind[i] += 1;
        if (ind[i] < dims[i])
        {
            return;
        }
        ind[i] = 0;
        --i;
    }
}

std::string IndiciesToStr(const std::vector<uint32_t> &ind, const std::vector<uint32_t> &base)
{
    const size_t ind_size = ind.size();
    if (ind_size < 1 || base.size() != ind_size)
    {
        return {};
    }

    std::ostringstream ss;
    const char *sep = "";
    for (size_t i = 0; i < ind_size; ++i)
    {
        ss << sep;
        sep = ", ";
        ss << (base[i] + ind[i]);
    }
    return ss.str();
}

using WalkFieldsCallback = std::function<HRESULT(mdFieldDef)>;
using WalkPropertiesCallback = std::function<HRESULT(mdProperty)>;

HRESULT ForEachFields(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkFieldsCallback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        if (FAILED(Status = cb(fieldDef)))
        {
            break;
        }
        else if (Status == S_FALSE)
        {
            Status = S_OK;
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

HRESULT ForEachProperties(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkPropertiesCallback &cb)
{
    HRESULT Status = S_OK;
    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    while (SUCCEEDED(pMDImport->EnumProperties(&propEnum, currentTypeDef, &propertyDef, 1, &numProperties)) &&
           numProperties != 0)
    {
        if (FAILED(Status = cb(propertyDef)))
        {
            break;
        }
    }
    pMDImport->CloseEnum(propEnum);
    return Status;
}

// https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L13
bool IsSynthesizedLocalName(WCHAR *mdName, ULONG nameLen)
{
    return (nameLen > 1 && starts_with(mdName, W("<"))) ||
           (nameLen > 4 && starts_with(mdName, W("CS$<")));
}

enum class GeneratedCodeKind : uint8_t
{
    Normal,
    Async,
    Lambda
};

HRESULT GetGeneratedCodeKind(IMetaDataImport *pMDImport, const WSTRING &methodName, mdTypeDef typeDef, GeneratedCodeKind &result)
{
    HRESULT Status = S_OK;
    std::array<WCHAR, mdNameLen> name{};
    ULONG nameLen = 0;
    IfFailRet(pMDImport->GetTypeDefProps(typeDef, name.data(), mdNameLen, &nameLen, nullptr, nullptr));
    const WSTRING typeName(name.data());

    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L24
    //  Parse the generated name. Returns true for names of the form
    //  [CS$]<[middle]>c[__[suffix]] where [CS$] is included for certain
    //  generated names, where [middle] and [__[suffix]] are optional,
    //  and where c is a single character in [1-9a-z]
    //  (csharp\LanguageAnalysis\LIB\SpecialName.cpp).

    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameKind.cs#L13-L20
    //  LambdaMethod = 'b',
    //  LambdaDisplayClass = 'c',
    //  StateMachineType = 'd',

    // https://github.com/dotnet/roslyn/blob/21055e1858548dbd8f4c1fd5d25a9c9617873806/src/Compilers/Core/Portable/PublicAPI.Shipped.txt#L252
    //  const Microsoft.CodeAnalysis.WellKnownMemberNames.MoveNextMethodName = "MoveNext" -> string!
    //  ... used in SynthesizedStateMachineMoveNextMethod class constructor.

    if (methodName.rfind(W("MoveNext"), 0) != WSTRING::npos && typeName.find(W(">d")) != WSTRING::npos)
    {
        result = GeneratedCodeKind::Async;
    }
    else if (methodName.find(W(">b")) != WSTRING::npos && typeName.find(W(">c")) != WSTRING::npos)
    {
        result = GeneratedCodeKind::Lambda;
    }
    else
    {
        result = GeneratedCodeKind::Normal;
    }

    return S_OK;
}

enum class GeneratedNameKind : uint8_t
{
    None,
    ThisProxyField,
    HoistedLocalField,
    DisplayClassLocalOrField
};

GeneratedNameKind GetLocalOrFieldNameKind(const WSTRING &localOrFieldName)
{
    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L24
    //  Parse the generated name. Returns true for names of the form
    //  [CS$]<[middle]>c[__[suffix]] where [CS$] is included for certain
    //  generated names, where [middle] and [__[suffix]] are optional,
    //  and where c is a single character in [1-9a-z]
    //  (csharp\LanguageAnalysis\LIB\SpecialName.cpp).

    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameKind.cs#L13-L20
    //  ThisProxyField = '4',
    //  HoistedLocalField = '5',
    //  DisplayClassLocalOrField = '8',

    if (localOrFieldName.find(W(">4")) != WSTRING::npos)
    {
        return GeneratedNameKind::ThisProxyField;
    }
    else if (localOrFieldName.find(W(">5")) != WSTRING::npos)
    {
        return GeneratedNameKind::HoistedLocalField;
    }
    else if (localOrFieldName.find(W(">8")) != WSTRING::npos)
    {
        return GeneratedNameKind::DisplayClassLocalOrField;
    }

    return GeneratedNameKind::None;
}

HRESULT GetClassAndTypeDefByValue(ICorDebugValue *pValue, ICorDebugClass **ppClass, mdTypeDef &typeDef)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    IfFailRet(trType->GetClass(ppClass));
    IfFailRet((*ppClass)->GetToken(&typeDef));
    return S_OK;
}

HRESULT FindThisProxyFieldValue(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                ICorDebugValue *pInputValue, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return E_INVALIDARG;
    }

    return ForEachFields(pMDImport, typeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            std::array<WCHAR, mdNameLen> mdName{};
            ULONG nameLen = 0;
            if (SUCCEEDED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen,
                                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                auto getValue = [&](ICorDebugValue **ppResultValue) -> HRESULT
                {
                    ToRelease<ICorDebugObjectValue> trObjValue;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                    IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
                    return S_OK;
                };

                const GeneratedNameKind generatedNameKind = GetLocalOrFieldNameKind(mdName.data());
                if (generatedNameKind == GeneratedNameKind::ThisProxyField)
                {
                    IfFailRet(getValue(ppResultValue));
                    return S_FALSE; // Fast exit from cycle
                }
                else if (generatedNameKind == GeneratedNameKind::DisplayClassLocalOrField)
                {
                    ToRelease<ICorDebugValue> trDisplayClassValue;
                    IfFailRet(getValue(&trDisplayClassValue));
                    ToRelease<ICorDebugClass> trDisplayClass;
                    mdTypeDef displayClassTypeDef = mdTypeDefNil;
                    IfFailRet(GetClassAndTypeDefByValue(trDisplayClassValue, &trDisplayClass, displayClassTypeDef));
                    IfFailRet(FindThisProxyFieldValue(pMDImport, trDisplayClass, displayClassTypeDef, trDisplayClassValue, ppResultValue));
                    if (ppResultValue != nullptr)
                    {
                        return S_FALSE; // Fast exit from cycle
                    }
                }
            }
            return S_OK; // Return with success to continue walk.
        });
}

// https://github.com/dotnet/roslyn/blob/3fdd28bc26238f717ec1124efc7e1f9c2158bce2/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L139-L159
HRESULT TryParseSlotIndex(const WSTRING &mdName, int32_t &index)
{
    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameConstants.cs#L11
    const WSTRING suffixSeparator(W("__"));
    const WSTRING::size_type suffixSeparatorOffset = mdName.rfind(suffixSeparator);
    if (suffixSeparatorOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    static constexpr size_t intMaxSizeInChars = 10;
    const WSTRING slotIndexString = mdName.substr(suffixSeparatorOffset + suffixSeparator.size());
    if (slotIndexString.empty() ||
        // Slot index is positive 4 byte int, that mean max is 10 characters (2147483647).
        slotIndexString.size() > intMaxSizeInChars)
    {
        return E_FAIL;
    }

    static constexpr int32_t base = 10;
    int32_t slotIndex = 0;
    for (const WCHAR wChar : slotIndexString)
    {
        if (wChar < W('0') || wChar > W('9'))
        {
            return E_FAIL;
        }

        slotIndex = (slotIndex * base) + static_cast<int32_t>(wChar - W('0'));
    }

    if (slotIndex < 1) // Slot index start from 1.
    {
        return E_FAIL;
    }

    index = slotIndex - 1;
    return S_OK;
}

// https://github.com/dotnet/roslyn/blob/3fdd28bc26238f717ec1124efc7e1f9c2158bce2/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L59
HRESULT TryParseHoistedLocalName(const WSTRING &mdName, WSTRING &wLocalName)
{
    WSTRING::size_type nameStartOffset = 0;
    if (mdName.length() > 1 && starts_with(mdName.data(), W("<")))
    {
        nameStartOffset = 1;
    }
    else if (mdName.length() > 4 && starts_with(mdName.data(), W("CS$<")))
    {
        nameStartOffset = 4;
    }
    else
    {
        return E_FAIL;
    }

    const WSTRING::size_type closeBracketOffset = mdName.find('>', nameStartOffset);
    if (closeBracketOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    wLocalName = mdName.substr(nameStartOffset, closeBracketOffset - nameStartOffset);
    return S_OK;
}

HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                 std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef,
                                 DebugInfo *pDebugInfo, ICorDebugModule *pModule,
                                 Evaluator::WalkStackVarsCallback cb)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return S_OK;
    }

    ToRelease<ICorDebugClass> trClass;
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(GetClassAndTypeDefByValue(trValue, &trClass, currentTypeDef));

    struct hoisted_local_scope_t
    {
        uint32_t startOffset;
        uint32_t length;
    };
    struct hoisted_local_scope_t_deleter
    {
        void operator()(hoisted_local_scope_t *p) const
        {
            Interop::CoTaskMemFree(p);
        }
    };

    int32_t hoistedLocalScopesCount = -1;
    std::unique_ptr<hoisted_local_scope_t, hoisted_local_scope_t_deleter> hoistedLocalScopes;

    return ForEachFields(pMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            std::array<WCHAR, mdNameLen> mdName{};
            ULONG nameLen = 0;
            DWORD fieldAttr = 0;
            if (FAILED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen,
                                                &fieldAttr, nullptr, nullptr, nullptr, nullptr, nullptr)) ||
                (fieldAttr & fdStatic) != 0 ||
                (fieldAttr & fdLiteral) != 0 ||
                usedNames.find(mdName.data()) != usedNames.end())
            {
                return S_OK; // Return with success to continue walk.
            }

            auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT
            {
                // Get pValue again, since it could be neutered at eval call in `cb` on previous cycle.
                trValue.Free();
                IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
                ToRelease<ICorDebugObjectValue> trObjValue;
                IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                return S_OK;
            };

            const GeneratedNameKind generatedNameKind = GetLocalOrFieldNameKind(mdName.data());
            if (generatedNameKind == GeneratedNameKind::DisplayClassLocalOrField)
            {
                ToRelease<ICorDebugValue> trDisplayClassValue;
                IfFailRet(getValue(&trDisplayClassValue, false));
                IfFailRet(WalkGeneratedClassFields(pMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                                   pDebugInfo, pModule, cb));
            }
            else if (generatedNameKind == GeneratedNameKind::HoistedLocalField)
            {
                if (hoistedLocalScopesCount == -1)
                {
                    void *data = nullptr;
                    if (SUCCEEDED(pDebugInfo->GetHoistedLocalScopes(pModule, methodDef, &data, hoistedLocalScopesCount)) && data)
                    {
                        hoistedLocalScopes.reset(static_cast<hoisted_local_scope_t *>(data));
                    }
                    else
                    {
                        hoistedLocalScopesCount = 0;
                    }
                }

                // Check, that hoisted local is in scope.
                // Note, in case we have any issue - ignore this check and show variable, since this is not fatal error.
                int32_t index = 0;
                if (hoistedLocalScopesCount > 0 && SUCCEEDED(TryParseSlotIndex(mdName.data(), index)) &&
                    hoistedLocalScopesCount > index &&
                    (currentIlOffset < hoistedLocalScopes.get()[index].startOffset ||
                    currentIlOffset >= hoistedLocalScopes.get()[index].startOffset + hoistedLocalScopes.get()[index].length))
                {
                    return S_OK; // Return with success to continue walk.
                }

                WSTRING wLocalName;
                if (FAILED(TryParseHoistedLocalName(mdName.data(), wLocalName)))
                {
                    return S_OK; // Return with success to continue walk.
                }

                IfFailRet(cb(to_utf8(wLocalName.data()), getValue));
                usedNames.insert(wLocalName);
            }
            // Ignore any other compiler generated fields, show only normal fields.
            else if (!IsSynthesizedLocalName(mdName.data(), nameLen))
            {
                IfFailRet(cb(to_utf8(mdName.data()), getValue));
                usedNames.insert(mdName.data());
            }
            return S_OK; // Return with success to continue walk.
        });
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
    return (corType == arg.corType && typeName == arg.typeName) ||
           isAlias(corType, arg.corType, arg.typeName) ||
           isAlias(arg.corType, corType, typeName);
}

SigElementType Evaluator::GetElementTypeByTypeName(const std::string &typeName)
{
    static const std::unordered_map<std::string, SigElementType> stypes = {
        {"void",    {ELEMENT_TYPE_VALUETYPE, "System.Void"}},
        {"bool",    {ELEMENT_TYPE_VALUETYPE, "System.Boolean"}},
        {"byte",    {ELEMENT_TYPE_VALUETYPE, "System.Byte"}},
        {"sbyte",   {ELEMENT_TYPE_VALUETYPE, "System.SByte"}},
        {"char",    {ELEMENT_TYPE_VALUETYPE, "System.Char"}},
        {"decimal", {ELEMENT_TYPE_VALUETYPE, "System.Decimal"}},
        {"double",  {ELEMENT_TYPE_VALUETYPE, "System.Double"}},
        {"float",   {ELEMENT_TYPE_VALUETYPE, "System.Single"}},
        {"int",     {ELEMENT_TYPE_VALUETYPE, "System.Int32"}},
        {"uint",    {ELEMENT_TYPE_VALUETYPE, "System.UInt32"}},
        {"long",    {ELEMENT_TYPE_VALUETYPE, "System.Int64"}},
        {"ulong",   {ELEMENT_TYPE_VALUETYPE, "System.UInt64"}},
        {"object",  {ELEMENT_TYPE_CLASS,     "System.Object"}},
        {"short",   {ELEMENT_TYPE_VALUETYPE, "System.Int16"}},
        {"ushort",  {ELEMENT_TYPE_VALUETYPE, "System.UInt16"}},
        {"string",  {ELEMENT_TYPE_CLASS,     "System.String"}},
        {"IntPtr",  {ELEMENT_TYPE_VALUETYPE, "System.IntPtr"}},
        {"UIntPtr", {ELEMENT_TYPE_VALUETYPE, "System.UIntPtr"}}
    };

    SigElementType userType;
    auto found = stypes.find(typeName);
    if (found != stypes.end())
    {
        return found->second;
    }
    userType.corType = ELEMENT_TYPE_CLASS;
    userType.typeName = typeName;
    return userType;
}

HRESULT Evaluator::GetElement(ICorDebugValue *pInputValue, std::vector<uint32_t> &indexes, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;

    if (indexes.empty())
    {
        return E_FAIL;
    }

    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;

    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugArrayValue> trArrayVal;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayVal)));

    uint32_t nRank = 0;
    IfFailRet(trArrayVal->GetRank(&nRank));

    if (indexes.size() != nRank)
    {
        return E_FAIL;
    }

    return trArrayVal->GetElement(static_cast<uint32_t>(indexes.size()), indexes.data(), ppResultValue);
}

HRESULT Evaluator::FollowNestedFindType(ICorDebugThread *pThread, const std::string &methodClass,
                                        std::vector<std::string> &identifiers, ICorDebugType **ppResultType)
{
    HRESULT Status = S_OK;

    std::vector<int> ranks;
    std::vector<std::string> classIdentifiers = EvalUtils::ParseType(methodClass, ranks);
    int nextClassIdentifier = 0;
    std::vector<std::string> fullpath;

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(EvalUtils::FindType(classIdentifiers, nextClassIdentifier, pThread, m_sharedDebugInfo.get(), nullptr, nullptr, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        fullpath = classIdentifiers;
        for (auto &identifier : identifiers)
        {
            fullpath.push_back(identifier);
        }

        nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(EvalUtils::FindType(fullpath, nextClassIdentifier, pThread, m_sharedDebugInfo.get(), trModule, &trType)))
        {
            break;
        }

        if (nextClassIdentifier == static_cast<int>(fullpath.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT Evaluator::WalkMethods(ICorDebugValue *pInputTypeValue, const WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pInputTypeValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    std::vector<SigElementType> methodGenerics;
    ToRelease<ICorDebugType> trResultType;

    return WalkMethods(trType, &trResultType, methodGenerics, cb);
}

HRESULT Evaluator::WalkMethods(ICorDebugType *pInputType, ICorDebugType **ppResultType,
                               std::vector<SigElementType> &methodGenerics,
                               const Evaluator::WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    pInputType->AddRef();
    ToRelease<ICorDebugType> trInputType(pInputType);

    while (trInputType != nullptr)
    {
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trInputType->GetClass(&trClass));
        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trClass->GetModule(&trModule));
        mdTypeDef currentTypeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&currentTypeDef));
        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        std::vector<SigElementType> typeGenerics;
        ToRelease<ICorDebugTypeEnum> trParamTypes;

        if (SUCCEEDED(trInputType->EnumerateTypeParameters(&trParamTypes)))
        {
            ULONG fetched = 0;
            ToRelease<ICorDebugType> trCurrentTypeParam;

            while (SUCCEEDED(trParamTypes->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
            {
                SigElementType argElType;
                trCurrentTypeParam->GetType(&argElType.corType);
                if (argElType.corType == ELEMENT_TYPE_VALUETYPE || argElType.corType == ELEMENT_TYPE_CLASS)
                {
                    IfFailRet(TypePrinter::NameForTypeByType(trCurrentTypeParam, argElType.typeName));
                }
                typeGenerics.emplace_back(argElType);
                trCurrentTypeParam.Free();
            }
        }

        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(trMDImport->EnumMethods(&fEnum, currentTypeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            mdTypeDef memTypeDef = mdTypeDefNil;
            ULONG nameLen = 0;
            std::array<WCHAR, mdNameLen> szFunctionName{};
            DWORD methodAttr = 0;
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), mdNameLen, &nameLen,
                                                &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            IfFailRet(ParseMethodSig(trMDImport, pSig, typeGenerics, methodGenerics, returnElementType, argElementTypes));
            if (Status == S_FALSE)
            {
                continue;
            }

            const bool is_static = ((methodAttr & mdStatic) != 0U);

            auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
            {
                return trModule->GetFunctionFromToken(methodDef, ppResultFunction);
            };

            IfFailRet(cb(is_static, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, getFunction));
            if (Status == S_FALSE)
            {
                continue;
            }

            *ppResultType = trInputType.Detach();
            trMDImport->CloseEnum(fEnum);
            return S_OK;
        }
        trMDImport->CloseEnum(fEnum);

        ToRelease<ICorDebugType> trBaseType;
        if (SUCCEEDED(trInputType->GetBase(&trBaseType)) && trBaseType != nullptr)
        {
            trInputType = trBaseType.Detach();
        }
        else
        {
            trInputType.Free();
        }
    }

    return S_OK;
}

HRESULT Evaluator::SetValue(ICorDebugThread *pThread, FrameLevel frameLevel, ToRelease<ICorDebugValue> &trPrevValue,
                            const GetValueCallback *getValue, SetterData *setterData, const std::string &value,
                            std::string &output)
{
    if (pThread == nullptr)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    std::string className;
    TypePrinter::GetTypeOfValue(trPrevValue, className);
    if (className.back() == '?') // System.Nullable<T>
    {
        ToRelease<ICorDebugValue> trValueValue;
        ToRelease<ICorDebugValue> trHasValueValue;
        IfFailRet(GetNullableValue(trPrevValue, &trValueValue, &trHasValueValue));

        if (value == "null")
        {
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trHasValueValue, "false", output));
        }
        else
        {
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trValueValue, value, output));
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trHasValueValue, "true", output));
        }
        if (getValue != nullptr)
        {
            trPrevValue.Free();
            IfFailRet((*getValue)(&trPrevValue, false));
        }
        return S_OK;
    }

    // In case this is not property, just change value itself.
    if (setterData == nullptr)
    {
        return m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trPrevValue, value, output);
    }

    trPrevValue->AddRef();
    ToRelease<ICorDebugValue> trValue(trPrevValue.GetPtr());
    CorElementType corType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&corType));

    if (corType == ELEMENT_TYPE_STRING)
    {
        // FIXME investigate, why in this case we can't use ICorDebugReferenceValue::SetValue() for string in trValue
        trValue.Free();
        IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(pThread, frameLevel, value, &trValue, output));

        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(trValue->GetType(&elemType));
        if (elemType != ELEMENT_TYPE_STRING)
        {
            return E_INVALIDARG;
        }
    }
    else // Allow stack machine decide what types are supported.
    {
        IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trValue.GetPtr(), value, output));
    }

    // Call setter.
    if (setterData->trThisValue == nullptr)
    {
        return m_sharedEvalHelpers->EvalFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetRef(),
                                                 1, trValue.GetRef(), 1, nullptr);
    }
    else
    {
        std::array<ICorDebugValue *, 2> ppArgsValue{setterData->trThisValue, trValue};
        return m_sharedEvalHelpers->EvalFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetRef(),
                                                 1, ppArgsValue.data(), 2, nullptr);
    }
}

HRESULT Evaluator::WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                               ICorDebugType *pTypeCast, bool provideSetterData, WalkMembersCallback cb)
{
    HRESULT Status = S_OK;

    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;

    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if ((isNull == TRUE) && (trValue.GetPtr() == nullptr))
    {
        return S_OK;
    }
    else if (trValue.GetPtr() == nullptr)
    {
        return E_FAIL;
    }

    CorElementType inputCorType = ELEMENT_TYPE_MAX;
    IfFailRet(pInputValue->GetType(&inputCorType));
    if (inputCorType == ELEMENT_TYPE_PTR)
    {
        auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT
        {
            trValue->AddRef();
            *ppResultValue = trValue;
            return S_OK;
        };

        return cb(nullptr, false, "", getValue, nullptr);
    }

    ToRelease<ICorDebugArrayValue> trArrayValue;
    if (SUCCEEDED(trValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayValue))))
    {
        uint32_t nRank = 0;
        IfFailRet(trArrayValue->GetRank(&nRank));

        uint32_t cElements = 0;
        IfFailRet(trArrayValue->GetCount(&cElements));

        std::vector<uint32_t> dims(nRank, 0);
        IfFailRet(trArrayValue->GetDimensions(nRank, dims.data()));

        std::vector<uint32_t> base(nRank, 0);
        BOOL hasBaseIndicies = FALSE;
        if (SUCCEEDED(trArrayValue->HasBaseIndicies(&hasBaseIndicies)) && (hasBaseIndicies == TRUE))
        {
            IfFailRet(trArrayValue->GetBaseIndicies(nRank, base.data()));
        }

        std::vector<uint32_t> ind(nRank, 0);

        for (uint32_t i = 0; i < cElements; ++i)
        {
            auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT {
                IfFailRet(trArrayValue->GetElementAtPosition(i, ppResultValue));
                return S_OK;
            };

            IfFailRet(cb(nullptr, false, "[" + IndiciesToStr(ind, base) + "]", getValue, nullptr));
            IncIndicies(ind, dims);
        }

        return S_OK;
    }

    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    if (pTypeCast == nullptr)
    {
        IfFailRet(trValue2->GetExactType(&trType));
        if (trType == nullptr)
        {
            return E_FAIL;
        }
    }
    else
    {
        pTypeCast->AddRef();
        trType = pTypeCast;
    }

    std::string className;
    TypePrinter::GetTypeOfValue(trType, className);
    if ((className == "decimal") || // TODO: implement mechanism for walking over custom type fields
        (className.back() == '?')) // System.Nullable<T>, don't provide class member list.
    {
        return S_OK;
    }

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(trType->GetType(&corElemType));
    if (corElemType == ELEMENT_TYPE_STRING)
    {
        return S_OK;
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    IfFailRet(ForEachFields(trMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            DWORD fieldAttr = 0;
            std::array<WCHAR, mdNameLen> mdName{};
            PCCOR_SIGNATURE pSignatureBlob = nullptr;
            ULONG sigBlobLength = 0;
            UVCP_CONSTANT pRawValue = nullptr;
            ULONG rawValueLength = 0;
            if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, &fieldAttr,
                                                    &pSignatureBlob, &sigBlobLength, nullptr, &pRawValue, &rawValueLength)))
            {
                // Prevent access to internal compiler added fields (without visible name).
                // Should be accessed by debugger routine only and hidden from user/ide.
                // More about compiler generated names in Roslyn sources:
                // https://github.com/dotnet/roslyn/blob/315c2e149ba7889b0937d872274c33fcbfe9af5f/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNames.cs
                // Note, uncontrolled access to internal compiler added field or its properties may break debugger work.
                if (IsSynthesizedLocalName(mdName.data(), nameLen))
                {
                    return S_OK; // Return with success to continue walk.
                }

                const bool is_static = (fieldAttr & fdStatic);
                if (isNull && !is_static)
                {
                    return S_OK; // Return with success to continue walk.
                }

                const std::string name = to_utf8(mdName.data());

                auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT
                {
                    if (fieldAttr & fdLiteral)
                    {
                        IfFailRet(m_sharedEvalHelpers->GetLiteralValue(pThread, trType, trModule, pSignatureBlob,
                                                                       sigBlobLength, pRawValue, rawValueLength,
                                                                       ppResultValue));
                    }
                    else if (fieldAttr & fdStatic)
                    {
                        if (pThread == nullptr)
                        {
                            return E_FAIL;
                        }

                        ToRelease<ICorDebugFrame> trFrame;
                        IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));

                        if (trFrame == nullptr)
                        {
                            return E_FAIL;
                        }

                        IfFailRet(trType->GetStaticFieldValue(fieldDef, trFrame, ppResultValue));
                    }
                    else
                    {
                        // Get trValue again, since it could be neutered at eval call in `cb` on previous cycle.
                        trValue.Free();
                        IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
                        ToRelease<ICorDebugObjectValue> trObjValue;
                        IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                        IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                    }

                    return S_OK; // Return with success to continue walk.
                };

                IfFailRet(cb(trType, is_static, name, getValue, nullptr));
            }
            return S_OK; // Return with success to continue walk.
        }));
    IfFailRet(ForEachProperties(trMDImport, currentTypeDef,
        [&](mdProperty propertyDef) -> HRESULT
        {
            mdTypeDef propertyClass = mdTypeDefNil;

            ULONG propertyNameLen = 0;
            UVCP_CONSTANT pDefaultValue;
            ULONG cchDefaultValue;
            mdMethodDef mdGetter = mdMethodDefNil;
            mdMethodDef mdSetter = mdMethodDefNil;
            std::array<WCHAR, mdNameLen> propertyName{};
            if (SUCCEEDED(trMDImport->GetPropertyProps(propertyDef, &propertyClass, propertyName.data(), mdNameLen,
                                                       &propertyNameLen, nullptr, nullptr, nullptr, nullptr, &pDefaultValue,
                                                       &cchDefaultValue, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
            {
                DWORD getterAttr = 0;
                if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                      nullptr, nullptr, nullptr, nullptr)))
                {
                    return S_OK; // Return with success to continue walk.
                }

                bool is_static = (getterAttr & mdStatic);
                if (isNull && !is_static)
                {
                    return S_OK; // Return with success to continue walk.
                }

                // https://github.com/dotnet/runtime/blob/737dcdda62ca847173ab50c905cd1604e70633b9/src/libraries/System.Private.CoreLib/src/System/Diagnostics/DebuggerBrowsableAttribute.cs#L16
                // Since we check only first byte, no reason store it as int (default enum type in c#)
                enum DebuggerBrowsableState : char // NOLINT(cppcoreguidelines-use-enum-class)
                {
                    Never = 0,
                    Expanded = 1,
                    Collapsed = 2,
                    RootHidden = 3
                };

                const char *g_DebuggerBrowsable = "System.Diagnostics.DebuggerBrowsableAttribute..ctor";
                bool debuggerBrowsableState_Never = false;

                ULONG numAttributes = 0;
                HCORENUM hEnum = nullptr;
                mdCustomAttribute attr = 0;
                while (SUCCEEDED(trMDImport->EnumCustomAttributes(&hEnum, propertyDef, 0, &attr, 1, &numAttributes)) &&
                       numAttributes != 0)
                {
                    mdToken ptkObj = mdTokenNil;
                    mdToken ptkType = mdTokenNil;
                    void const *ppBlob = nullptr;
                    ULONG pcbSize = 0;
                    if (FAILED(trMDImport->GetCustomAttributeProps(attr, &ptkObj, &ptkType, &ppBlob, &pcbSize)))
                    {
                        continue;
                    }

                    std::string mdName;
                    if (FAILED(TypePrinter::NameForToken(ptkType, trMDImport, mdName, true, nullptr)))
                    {
                        continue;
                    }

                    if (mdName == g_DebuggerBrowsable
                        // In case of DebuggerBrowsableAttribute blob is 8 bytes:
                        // 2 bytes - blob prolog 0x0001
                        // 4 bytes - data (DebuggerBrowsableAttribute::State), default enum type (int)
                        // 2 bytes - alignment
                        // We check only one byte (first data byte), no reason check 4 bytes in our case.
                        && pcbSize > 2 && (static_cast<char const *>(ppBlob)[2] == DebuggerBrowsableState::Never))
                    {
                        debuggerBrowsableState_Never = true;
                        break;
                    }
                }
                trMDImport->CloseEnum(hEnum);

                if (debuggerBrowsableState_Never)
                {
                    return S_OK; // Return with success to continue walk.
                }

                const std::string name = to_utf8(propertyName.data());

                auto getValue =
                    [&](ICorDebugValue **ppResultValue, bool ignoreEvalFlags) -> HRESULT
                    {
                        if (pThread == nullptr)
                        {
                            return E_FAIL;
                        }

                        ToRelease<ICorDebugFunction> trFunc;
                        IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                        return m_sharedEvalHelpers->EvalFunction(pThread, trFunc, trType.GetRef(), 1,
                                                                 is_static ? nullptr : &pInputValue, is_static ? 0 : 1,
                                                                 ppResultValue, ignoreEvalFlags);
                    };

                if (provideSetterData)
                {
                    ToRelease<ICorDebugFunction> trFuncSetter;
                    if (FAILED(trModule->GetFunctionFromToken(mdSetter, &trFuncSetter)))
                    {
                        trFuncSetter.Free();
                    }
                    Evaluator::SetterData setterData(is_static ? nullptr : pInputValue, trType, trFuncSetter);
                    IfFailRet(cb(trType, is_static, name, getValue, &setterData));
                }
                else
                {
                    IfFailRet(cb(trType, is_static, name, getValue, nullptr));
                }
            }
            return S_OK; // Return with success to continue walk.
        }));

    std::string baseTypeName;
    ToRelease<ICorDebugType> trBaseType;
    if (SUCCEEDED(trType->GetBase(&trBaseType)) && trBaseType != nullptr &&
        SUCCEEDED(TypePrinter::GetTypeOfValue(trBaseType, baseTypeName)))
    {
        if (baseTypeName == "System.Enum")
        {
            return S_OK;
        }
        else if (baseTypeName != "object" && baseTypeName != "System.Object" && baseTypeName != "System.ValueType")
        {
            if (pThread != nullptr)
            {
                // Note, this call could return S_FALSE without ICorDebugValue creation in case type don't have static members.
                IfFailRet(m_sharedEvalHelpers->CreatTypeObjectStaticConstructor(pThread, trBaseType));
            }
            // Add fields of base class
            IfFailRet(WalkMembers(pInputValue, pThread, frameLevel, trBaseType, provideSetterData, cb));
        }
    }

    return S_OK;
}

// Note, this method return Class name, not Type name (will not provide generic initialization types if any).
HRESULT Evaluator::GetMethodClass(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &methodClass, bool &haveThis)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFrame->GetFunction(&trFunction));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));

    DWORD methodAttr = 0;
    std::array<WCHAR, mdNameLen> szMethod{};
    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), mdNameLen, &szMethodLen,
                                         &methodAttr, nullptr, nullptr, nullptr, nullptr));

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));
    // We are inside method of this class, if typeDef is not TypeDef token - something definitely going wrong.
    if (TypeFromToken(typeDef) != mdtTypeDef)
    {
        return E_FAIL;
    }

    haveThis = ((methodAttr & mdStatic) == 0);
    // In case this is static method, this is not async/lambda case for sure.
    if (!haveThis)
    {
        return TypePrinter::NameForTypeDef(typeDef, trMDImport, methodClass, nullptr);
    }

    GeneratedCodeKind generatedCodeKind = GeneratedCodeKind::Normal;
    IfFailRet(GetGeneratedCodeKind(trMDImport, szMethod.data(), typeDef, generatedCodeKind));
    if (generatedCodeKind == GeneratedCodeKind::Normal)
    {
        return TypePrinter::NameForTypeDef(typeDef, trMDImport, methodClass, nullptr);
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    ToRelease<ICorDebugValue> trCurrentThis;
    IfFailRet(trILFrame->GetArgument(0, &trCurrentThis));

    // Check do we have real This value (that should be stored in ThisProxyField).
    ToRelease<ICorDebugValue> trUserThis;
    IfFailRet(FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
    haveThis = (trUserThis != nullptr);

    // Find first user code enclosing class, since compiler add async/lambda as nested class.
    do
    {
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        IfFailRet(trMDImport->GetTypeDefProps(typeDef, mdName.data(), mdNameLen, &nameLen, nullptr, nullptr));

        if (!IsSynthesizedLocalName(mdName.data(), nameLen))
        {
            break;
        }

        mdTypeDef enclosingClass = mdTypeDefNil;
        if (SUCCEEDED(Status = trMDImport->GetNestedClassProps(typeDef, &enclosingClass)))
        {
            typeDef = enclosingClass;
        }
        else
        {
            return Status;
        }
    } while (true);

    return TypePrinter::NameForTypeDef(typeDef, trMDImport, methodClass, nullptr);
}

HRESULT Evaluator::WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    uint32_t currentIlOffset = 0;
    SequencePoint sp;
    // GetFrameILAndSequencePoint() return "success" code only in case it found sequence point
    // for current IP, that mean we stop inside user code.
    // Note, we could have request for not user code, we ignore it and this is OK.
    if (FAILED(m_sharedDebugInfo->GetFrameILAndSequencePoint(trFrame, currentIlOffset, sp)))
    {
        return S_OK;
    }

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFrame->GetFunction(&trFunction));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    ToRelease<ICorDebugValueEnum> trLocalsEnum;
    IfFailRet(trILFrame->EnumerateLocalVariables(&trLocalsEnum));

    ULONG cLocals = 0;
    IfFailRet(trLocalsEnum->GetCount(&cLocals));

    ULONG cArguments = 0;
    ToRelease<ICorDebugValueEnum> trArgumentEnum;
    IfFailRet(trILFrame->EnumerateArguments(&trArgumentEnum));
    IfFailRet(trArgumentEnum->GetCount(&cArguments));

    // Note, we use same order as vsdbg use:
    // 1. "this" (real or "this" proxy field in case async method and lambda).
    // 2. "real" argumens.
    // 3. "real" local variables.
    // 4. async/lambda object fields.

    DWORD methodAttr = 0;
    std::array<WCHAR, mdNameLen> szMethod{};
    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), mdNameLen, &szMethodLen,
                                         &methodAttr, nullptr, nullptr, nullptr, nullptr));

    GeneratedCodeKind generatedCodeKind = GeneratedCodeKind::Normal;
    ToRelease<ICorDebugValue> trCurrentThis; // Current This. Note, in case async method or lambda - this is special object (not user's "this").
    // In case this is static method, this is not async/lambda case for sure.
    if ((methodAttr & mdStatic) == 0)
    {
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trFunction->GetClass(&trClass));
        mdTypeDef typeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&typeDef));
        IfFailRet(GetGeneratedCodeKind(trMDImport, szMethod.data(), typeDef, generatedCodeKind));
        IfFailRet(trILFrame->GetArgument(0, &trCurrentThis));

        ToRelease<ICorDebugValue> trUserThis;
        if (generatedCodeKind == GeneratedCodeKind::Normal)
        {
            trCurrentThis->AddRef();
            trUserThis = trCurrentThis.GetPtr();
        }
        else
        {
            // Check do we have real This value (that should be stored in ThisProxyField).
            IfFailRet(FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
        }

        if (trUserThis != nullptr)
        {
            auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT
            {
                trUserThis->AddRef();
                *ppResultValue = trUserThis;
                return S_OK;
            };
            IfFailRet(cb("this", getValue));
            // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
            trFrame.Free();
            trILFrame.Free();
        }
    }

    // Lambda could duplicate arguments into display class local object. Make sure we call "cb" only once for unique name.
    // Note, we don't use usedNames with 'this' related code above, since it have logic "find first and return".
    // In the same time, all code below ignore 'this' argument/field check.
    std::unordered_set<WSTRING> usedNames;

    for (ULONG i = (methodAttr & mdStatic) == 0 ? 1 : 0; i < cArguments; i++)
    {
        // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
        // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
        // Note, IMetaDataImport::GetParamForMethodIndex() don't include "this", but ICorDebugILFrame::GetArgument() do. This is why we have different logic here.
        const ULONG idx = ((methodAttr & mdStatic) == 0) ? i : (i + 1);
        std::array<WCHAR, mdNameLen> wParamName{};
        ULONG paramNameLen = 0;
        mdParamDef paramDef = mdParamDefNil;
        if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
            FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), mdNameLen,
                                             &paramNameLen, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT {
            if (trFrame == nullptr) // Forced to update trFrame/trILFrame.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                if (trFrame == nullptr)
                {
                    return E_FAIL;
                }
                IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
            }
            return trILFrame->GetArgument(i, ppResultValue);
        };

        IfFailRet(cb(to_utf8(wParamName.data()), getValue));
        usedNames.insert(wParamName.data());
        // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    for (uint32_t i = 0; i < cLocals; i++)
    {
        WSTRING wLocalName;
        int32_t ilStart = 0;
        int32_t ilEnd = 0;
        if (FAILED(m_sharedDebugInfo->GetFrameNamedLocalVariable(trModule, methodDef, i, wLocalName, &ilStart, &ilEnd)) ||
            ilStart < 0 ||
            ilEnd < 0 ||
            currentIlOffset < static_cast<uint32_t>(ilStart) ||
            currentIlOffset >= static_cast<uint32_t>(ilEnd))
        {
            continue;
        }

        auto getValue = [&](ICorDebugValue **ppResultValue, bool) -> HRESULT
        {
            if (trFrame == nullptr) // Forced to update trFrame/trILFrame.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                if (trFrame == nullptr)
                {
                    return E_FAIL;
                }
                IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
            }
            return trILFrame->GetLocalVariable(i, ppResultValue);
        };

        // Note, this method could have lambdas inside, display class local objects must be also checked,
        // since this objects could hold current method local variables too.
        if (GetLocalOrFieldNameKind(wLocalName) == GeneratedNameKind::DisplayClassLocalOrField)
        {
            ToRelease<ICorDebugValue> trDisplayClassValue;
            IfFailRet(getValue(&trDisplayClassValue, false));
            IfFailRet(WalkGeneratedClassFields(trMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                               m_sharedDebugInfo.get(), trModule, cb));
            continue;
        }

        IfFailRet(cb(to_utf8(wLocalName.data()), getValue));
        usedNames.insert(wLocalName);
        // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    if (generatedCodeKind != GeneratedCodeKind::Normal)
    {
        return WalkGeneratedClassFields(trMDImport, trCurrentThis, currentIlOffset, usedNames, methodDef, m_sharedDebugInfo.get(), trModule, cb);
    }

    return S_OK;
}

HRESULT Evaluator::FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                                ValueKind valueKind, std::vector<std::string> &identifiers,
                                int nextIdentifier, ICorDebugValue **ppResult,
                                std::unique_ptr<Evaluator::SetterData> *resultSetterData)
{
    HRESULT Status = S_OK;

    // Note, in case of (nextIdentifier == identifiers.size()) result is pValue itself, so, we ok here.
    if (nextIdentifier > static_cast<int>(identifiers.size()))
    {
        return E_FAIL;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trResultValue(pValue);
    for (int i = nextIdentifier; i < static_cast<int>(identifiers.size()); i++)
    {
        if (identifiers[i].empty())
        {
            return E_FAIL;
        }

        const ToRelease<ICorDebugValue> trClassValue(trResultValue.Detach());

        WalkMembers(trClassValue, pThread, frameLevel, nullptr, (resultSetterData != nullptr),
                    [&](ICorDebugType */*pType*/, bool is_static, const std::string &memberName,
                        const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData)
                        {
                            if ((is_static && valueKind == ValueKind::Variable) ||
                                (!is_static && valueKind == ValueKind::Class) ||
                                memberName != identifiers[i])

                            {
                                return S_OK;
                            }

                            IfFailRet(getValue(&trResultValue, false));
                            if (setterData != nullptr &&
                                resultSetterData != nullptr)
                            {
                                *resultSetterData = std::make_unique<Evaluator::SetterData>(*setterData);
                            }

                            return E_ABORT; // Fast exit from cycle with result.
                        });

        if (trResultValue == nullptr)
        {
            return E_FAIL;
        }

        valueKind = ValueKind::Variable; // we can only follow through instance fields
    }

    *ppResult = trResultValue.Detach();
    return S_OK;
}

HRESULT Evaluator::FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel,
                                         const std::string &methodClass, std::vector<std::string> &identifiers,
                                         ICorDebugValue **ppResult,
                                         std::unique_ptr<Evaluator::SetterData> *resultSetterData)
{
    HRESULT Status = S_OK;

    std::vector<int> ranks;
    std::vector<std::string> classIdentifiers = EvalUtils::ParseType(methodClass, ranks);
    int nextClassIdentifier = 0;
    const int identifiersNum = static_cast<int>(identifiers.size()) - 1;
    std::vector<std::string> fieldName{identifiers.back()};
    std::vector<std::string> fullpath;

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(EvalUtils::FindType(classIdentifiers, nextClassIdentifier, pThread, m_sharedDebugInfo.get(), nullptr, nullptr, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        ToRelease<ICorDebugType> trType;
        nextClassIdentifier = 0;
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        fullpath = classIdentifiers;
        for (int i = 0; i < identifiersNum; i++)
        {
            fullpath.push_back(identifiers[i]);
        }

        if (FAILED(EvalUtils::FindType(fullpath, nextClassIdentifier, pThread, m_sharedDebugInfo.get(), trModule, &trType)))
        {
            break;
        }

        if (nextClassIdentifier < static_cast<int>(fullpath.size()))
        {
            // try to check non-static fields inside a static member
            std::vector<std::string> staticName;
            for (int i = nextClassIdentifier; i < static_cast<int>(fullpath.size()); i++)
            {
                staticName.emplace_back(fullpath[i]);
            }
            staticName.emplace_back(fieldName[0]);
            ToRelease<ICorDebugValue> trTypeObject;
            if (S_OK == m_sharedEvalHelpers->CreatTypeObjectStaticConstructor(pThread, trType, &trTypeObject))
            {
                if (SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Class, staticName, 0,
                                           ppResult, resultSetterData)))
                {
                    return S_OK;
                }
            }
            trim = true;
            continue;
        }

        ToRelease<ICorDebugValue> trTypeObject;
        IfFailRet(m_sharedEvalHelpers->CreatTypeObjectStaticConstructor(pThread, trType, &trTypeObject));
        if (Status == S_OK && // type have static members (S_FALSE if type don't have static members)
            SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Class, fieldName,
                                   0, ppResult, resultSetterData)))
        {
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT Evaluator::ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pInputValue,
                                      SetterData *inputSetterData, std::vector<std::string> &identifiers,
                                      ICorDebugValue **ppResultValue, std::unique_ptr<SetterData> *resultSetterData,
                                      ICorDebugType **ppResultType)
{
    if ((pInputValue != nullptr) && identifiers.empty())
    {
        pInputValue->AddRef();
        *ppResultValue = pInputValue;
        if ((inputSetterData != nullptr) && (resultSetterData != nullptr))
        {
            *resultSetterData = std::make_unique<Evaluator::SetterData>(*inputSetterData);
        }
        return S_OK;
    }
    else if (pInputValue != nullptr)
    {
        return FollowFields(pThread, frameLevel, pInputValue, ValueKind::Variable, identifiers, 0, ppResultValue,
                            resultSetterData);
    }

    HRESULT Status = S_OK;
    int nextIdentifier = 0;
    ToRelease<ICorDebugValue> trResolvedValue;
    ToRelease<ICorDebugValue> trThisValue;

    if (identifiers.at(nextIdentifier) == "$exception")
    {
        IfFailRet(pThread->GetCurrentException(&trResolvedValue));
        if (trResolvedValue == nullptr)
        {
            return E_FAIL;
        }
    }
    else
    {
        // Note, we use E_ABORT error code as fast way to exit from stack vars walk routine here.
        if (FAILED(Status = WalkStackVars(pThread, frameLevel,
                [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
                {
                    if (name == "this")
                    {
                        if (FAILED(getValue(&trThisValue, false)) || (trThisValue == nullptr))
                        {
                            return S_OK;
                        }

                        if (name == identifiers.at(nextIdentifier))
                        {
                            return E_ABORT; // Fast way to exit from stack vars walk routine.
                        }
                    }
                    else if (name == identifiers.at(nextIdentifier))
                    {
                        if (FAILED(getValue(&trResolvedValue, false)) || (trResolvedValue == nullptr))
                        {
                            return S_OK;
                        }

                        return E_ABORT; // Fast way to exit from stack vars walk routine.
                    }

                    return S_OK;
                })) &&
            (trThisValue == nullptr) && (trResolvedValue == nullptr)) // Check, that we have fast exit instead of real error.
        {
            return Status;
        }
    }

    if ((trResolvedValue == nullptr) && (trThisValue != nullptr)) // check this/this.*
    {
        if (identifiers[nextIdentifier] == "this")
        {
            nextIdentifier++; // skip first identifier with "this" (we have it in trThisValue), check rest
        }

        if (SUCCEEDED(FollowFields(pThread, frameLevel, trThisValue, ValueKind::Variable, identifiers,
                                   nextIdentifier, &trResolvedValue, resultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }
    }

    if (trResolvedValue == nullptr) // check statics in nested classes
    {
        ToRelease<ICorDebugFrame> trFrame;
        IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        std::string methodClass;
        std::string methodName;
        TypePrinter::GetTypeAndMethod(trFrame, methodClass, methodName);

        if (SUCCEEDED(FollowNestedFindValue(pThread, frameLevel, methodClass, identifiers, &trResolvedValue,
                                            resultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }

        if ((ppResultType != nullptr) && 
            SUCCEEDED(FollowNestedFindType(pThread, methodClass, identifiers, ppResultType)))
        {
            return S_OK;
        }
    }

    ValueKind valueKind = ValueKind::Variable;
    if (trResolvedValue != nullptr)
    {
        nextIdentifier++;
        if (nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }
        valueKind = ValueKind::Variable;
    }
    else
    {
        ToRelease<ICorDebugType> trType;
        IfFailRet(EvalUtils::FindType(identifiers, nextIdentifier, pThread, m_sharedDebugInfo.get(), nullptr, &trType));
        IfFailRet(m_sharedEvalHelpers->CreatTypeObjectStaticConstructor(pThread, trType, &trResolvedValue));

        // Identifiers resolved into type, not value. In case type could be result - provide type directly as result.
        // In this way caller will know, that no object instance here (should operate with static members/methods only).
        if ((ppResultType != nullptr) && nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        if (Status == S_FALSE || // type don't have static members, nothing explore here
            nextIdentifier == static_cast<int>(identifiers.size())) // trResolvedValue is temporary object for members exploration, can't be result
        {
            return E_INVALIDARG;
        }

        valueKind = ValueKind::Class;
    }

    ToRelease<ICorDebugValue> trResultValue;
    IfFailRet(FollowFields(pThread, frameLevel, trResolvedValue, valueKind, identifiers, nextIdentifier, &trResultValue, resultSetterData));

    *ppResultValue = trResultValue.Detach();
    return S_OK;
}

HRESULT Evaluator::LookupExtensionMethods(ICorDebugType *pType, const std::string &methodName,
                                          std::vector<SigElementType> &methodArgs,
                                          std::vector<SigElementType> &methodGenerics,
                                          ICorDebugFunction **ppCorFunc)
{
    static constexpr std::string_view attributeName("System.Runtime.CompilerServices.ExtensionAttribute..ctor");
    HRESULT Status = S_OK;
    std::vector<SigElementType> typeGenerics;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;

    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG fetched = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
        {
            SigElementType argElType;
            trCurrentTypeParam->GetType(&argElType.corType);
            if (argElType.corType == ELEMENT_TYPE_VALUETYPE || argElType.corType == ELEMENT_TYPE_CLASS)
            {
                IfFailRet(TypePrinter::NameForTypeByType(trCurrentTypeParam, argElType.typeName));
            }
            typeGenerics.emplace_back(argElType);
            trCurrentTypeParam.Free();
        }
    }

    IfFailRet(m_sharedDebugInfo->ForEachModule([&](ICorDebugModule *pModule) -> HRESULT
    {
        ULONG typesCnt = 0;
        HCORENUM fTypeEnum = nullptr;
        mdTypeDef mdType = mdTypeDefNil;

        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        while (SUCCEEDED(trMDImport->EnumTypeDefs(&fTypeEnum, &mdType, 1, &typesCnt)) && typesCnt != 0)
        {
            std::string typeName;
            if (!HasAttribute(trMDImport, mdType, attributeName) ||
                FAILED(TypePrinter::NameForToken(mdType, trMDImport, typeName, false, nullptr)))
            {
                continue;
            }
            HCORENUM fFuncEnum = nullptr;
            mdMethodDef mdMethod = mdMethodDefNil;
            ULONG methodsCnt = 0;

            while (SUCCEEDED(trMDImport->EnumMethods(&fFuncEnum, mdType, &mdMethod, 1, &methodsCnt)) && methodsCnt != 0)
            {
                mdTypeDef memTypeDef = mdTypeDefNil;
                ULONG nameLen = 0;
                std::array<WCHAR, mdNameLen> szFuncName{};
                PCCOR_SIGNATURE pSig = nullptr;
                ULONG cbSig = 0;

                if (FAILED(trMDImport->GetMethodProps(mdMethod, &memTypeDef, szFuncName.data(), mdNameLen, &nameLen,
                                                      nullptr, &pSig, &cbSig, nullptr, nullptr)) ||
                    !HasAttribute(trMDImport, mdMethod, attributeName))
                {
                    continue;
                }

                const std::string fullName = to_utf8(szFuncName.data());
                if (fullName != methodName)
                {
                    continue;
                }

                // TODO use ParseMethodSig() for sig parsing:
                //    SigElementType returnElementType;
                //    std::vector<SigElementType> argElementTypes;
                //    IfFailRet(ParseMethodSig(trMDImport, pSig, typeGenerics, methodGenerics, returnElementType, argElementTypes));
                //    if (Status == S_FALSE)
                //    {
                //        continue;
                //    }

                ULONG cParams = 0; // Count of signature parameters.
                ULONG gParams = 0; // count of generic parameters;
                ULONG elementSize = 0;
                ULONG convFlags = 0;

                // 1. calling convention for MethodDefSig:
                // [[HASTHIS] [EXPLICITTHIS]] (DEFAULT|VARARG|GENERIC GenParamCount)
                elementSize = CorSigUncompressData(pSig, &convFlags);
                pSig += elementSize;

                // 2. if method has generic params, count them
                constexpr ULONG SIG_METHOD_GENERIC = 0x10; // used to indicate that the method has one or more generic parameters.
                if ((convFlags & SIG_METHOD_GENERIC) != 0U)
                {
                    elementSize = CorSigUncompressData(pSig, &gParams);
                    pSig += elementSize;
                }

                // 3. count of params
                elementSize = CorSigUncompressData(pSig, &cParams);
                pSig += elementSize;

                // 4. return type
                SigElementType returnElementType;
                if (FAILED(ParseElementType(trMDImport, &pSig, returnElementType, typeGenerics, methodGenerics)))
                {
                    continue;
                }

                // 5. get next element from method signature
                std::vector<SigElementType> argElementTypes(cParams);
                for (ULONG i = 0; i < cParams; ++i)
                {
                    if (FAILED(ParseElementType(trMDImport, &pSig, argElementTypes[i], typeGenerics, methodGenerics)))
                    {
                        break;
                    }
                }

                std::string typeName;
                CorElementType ty = ELEMENT_TYPE_MAX;

                if (FAILED(pType->GetType(&ty)) ||
                    FAILED(TypePrinter::NameForTypeByType(pType, typeName)))
                {
                    continue;
                }
                if (ty == ELEMENT_TYPE_CLASS || ty == ELEMENT_TYPE_VALUETYPE)
                {
                    if (typeName != argElementTypes[0].typeName)
                    {
                        // if type names don't match check implemented interfaces names

                        ToRelease<ICorDebugClass> trClass;
                        if (FAILED(pType->GetClass(&trClass)))
                        {
                            continue;
                        }

                        ToRelease<ICorDebugModule> trModule;
                        if (FAILED(trClass->GetModule(&trModule)))
                        {
                            continue;
                        }

                        mdTypeDef metaTypeDef = mdTypeDefNil;
                        if (FAILED(trClass->GetToken(&metaTypeDef)))
                        {
                            continue;
                        }

                        ToRelease<IUnknown> trUnknownInt;
                        if (FAILED(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknownInt)))
                        {
                            continue;
                        }

                        ToRelease<IMetaDataImport> trMDImportInt;
                        if (FAILED(trUnknownInt->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImportInt))))
                        {
                            continue;
                        }

                        HCORENUM ifEnum = nullptr;
                        mdInterfaceImpl ifaceImpl = mdInterfaceImplNil;
                        ULONG pcImpls = 0;
                        while (SUCCEEDED(trMDImportInt->EnumInterfaceImpls(&ifEnum, metaTypeDef, &ifaceImpl, 1, &pcImpls)) &&
                               pcImpls != 0)
                        {
                            mdTypeDef tkClass = mdTypeDefNil;
                            mdToken tkIface = mdTokenNil;
                            PCCOR_SIGNATURE pSig = nullptr;
                            ULONG pcbSig = 0;
                            SigElementType ifaceElementType;
                            if (FAILED(trMDImportInt->GetInterfaceImplProps(ifaceImpl, &tkClass, &tkIface)))
                            {
                                continue;
                            }
                            if (TypeFromToken(tkIface) == mdtTypeSpec)
                            {
                                if (FAILED(trMDImportInt->GetTypeSpecFromToken(tkIface, &pSig, &pcbSig)) ||
                                    FAILED(ParseElementType(trMDImportInt, &pSig, ifaceElementType, typeGenerics, methodGenerics, false)))
                                {
                                    continue;
                                }
                            }
                            else
                            {
                                if (FAILED(TypePrinter::NameForToken(tkIface, trMDImportInt, ifaceElementType.typeName, true, nullptr)))
                                {
                                    continue;
                                }
                            }

                            if (ifaceElementType.typeName == argElementTypes[0].typeName &&
                                methodArgs.size() + 1 == argElementTypes.size())
                            {
                                bool found = true;
                                for (unsigned int i = 0; i < methodArgs.size(); i++)
                                {
                                    if (methodArgs[i].corType != argElementTypes[i + 1].corType)
                                    {
                                        found = false;
                                        break;
                                    }
                                }
                                if (found)
                                {
                                    pModule->GetFunctionFromToken(mdMethod, ppCorFunc);
                                    trMDImportInt->CloseEnum(ifEnum);
                                    trMDImport->CloseEnum(fFuncEnum);
                                    trMDImport->CloseEnum(fTypeEnum);
                                    return S_FALSE; // Fast exit from cycle
                                }
                            }
                        }
                        trMDImportInt->CloseEnum(ifEnum);
                    }
                }
                else if (ty != argElementTypes[0].corType || (methodArgs.size() + 1 != argElementTypes.size()))
                {
                    continue;
                }
                else
                {
                    bool found = true;
                    for (unsigned int i = 0; i < methodArgs.size(); i++)
                    {
                        if (methodArgs[i].corType != argElementTypes[i + 1].corType)
                        {
                            found = false;
                            break;
                        }
                    }
                    if (found)
                    {
                        pModule->GetFunctionFromToken(mdMethod, ppCorFunc);
                        trMDImport->CloseEnum(fFuncEnum);
                        trMDImport->CloseEnum(fTypeEnum);
                        return S_FALSE; // Fast exit from cycle
                    }
                }
            }
            trMDImport->CloseEnum(fFuncEnum);
        }
        trMDImport->CloseEnum(fTypeEnum);
        return S_OK; // Return with success to continue walk.
    }));
    return S_OK;
}

} // namespace dncdbg
