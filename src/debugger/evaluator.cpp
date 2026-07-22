// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluator.h"
#include "debugger/evalhelpers.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalwaiter.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalutils.h"
#include "debugger/frames.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h"
#include "metadata/attributes.h"
#include "metadata/modules.h"
#include "metadata/sigparse.h"
#include "metadata/typeprinter.h"
#include "utils/hresult.h"
#include "utils/filesystem.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cassert>
#include <cstring>
#include <iterator>
#include <list>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

namespace dncdbg
{

namespace
{

void IncIndices(const std::vector<uint32_t> &dims, std::vector<uint32_t> &ind)
{
    assert(ind.size() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
    int i = static_cast<int32_t>(ind.size()) - 1;

    while (i >= 0)
    {
        ind.at(i) += 1;
        if (ind.at(i) < dims.at(i))
        {
            return;
        }
        ind.at(i) = 0;
        --i;
    }
}

std::string IndicesToStr(const std::vector<uint32_t> &ind, const std::vector<uint32_t> &base)
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
        ss << (base.at(i) + ind.at(i));
    }
    return ss.str();
}

using WalkFieldsCallback = std::function<HRESULT(mdFieldDef)>;
using WalkPropertiesCallback = std::function<HRESULT(mdProperty)>;

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachFields(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkFieldsCallback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        if (FAILED(Status = cb(fieldDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachProperties(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkPropertiesCallback &cb)
{
    HRESULT Status = S_OK;
    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    while (SUCCEEDED(pMDImport->EnumProperties(&propEnum, currentTypeDef, &propertyDef, 1, &numProperties)) &&
           numProperties != 0)
    {
        if (FAILED(Status = cb(propertyDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(propEnum);
    return Status;
}

// https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L13
bool IsSynthesizedLocalName(const WSTRING &mdName)
{
    return mdName.find(W('<')) == 0 ||
           mdName.find(W("CS$<")) == 0;
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
    ULONG nameLen = 0;
    IfFailRet(pMDImport->GetTypeDefProps(typeDef, nullptr, 0, &nameLen, nullptr, nullptr));

    WSTRING typeName(nameLen, '\0');
    IfFailRet(pMDImport->GetTypeDefProps(typeDef, typeName.data(), nameLen, nullptr, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!typeName.empty() && typeName.back() == '\0')
    {
        typeName.pop_back();
    }

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
    DisplayClassLocalOrField,
    PrimaryConstructorParameterField
};

GeneratedNameKind GetLocalOrFieldNameKind(const WSTRING &localOrFieldName)
{
    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L24
    //  Parse the generated name. Returns true for names of the form
    //  [CS$]<[middle]>c[__[suffix]] where [CS$] is included for certain
    //  generated names, where [middle] and [__[suffix]] are optional,
    //  and where c is a single character in [1-9a-z]
    //  (csharp\LanguageAnalysis\LIB\SpecialName.cpp).

    // https://github.com/dotnet/roslyn/blob/f7c7a5972ea0c8c645ddef58ec00a0e03136fd70/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameKind.cs#L13-L20
    //  ThisProxyField = '4'
    //  HoistedLocalField = '5'
    //  DisplayClassLocalOrField = '8'
    //  PrimaryConstructorParameter = 'P'

    if (localOrFieldName.length() <= 3)
    {
        return GeneratedNameKind::None;
    }

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
    else if (localOrFieldName.find(W(">P")) != WSTRING::npos)
    {
        return GeneratedNameKind::PrimaryConstructorParameterField;
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

    Status = ForEachFields(pMDImport, typeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

            WSTRING mdName(nameLen, '\0');
            if (SUCCEEDED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                // Remove null terminator that was included in the length
                if (!mdName.empty() && mdName.back() == '\0')
                {
                    mdName.pop_back();
                }

                auto getValue = [&](ICorDebugValue **ppResultValue) -> HRESULT
                {
                    ToRelease<ICorDebugObjectValue> trObjValue;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                    IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
                    return S_OK;
                };

                const GeneratedNameKind generatedNameKind = GetLocalOrFieldNameKind(mdName);
                if (generatedNameKind == GeneratedNameKind::ThisProxyField)
                {
                    IfFailRet(getValue(ppResultValue));
                    return S_CAN_EXIT; // Fast exit from loop.
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
                        return S_CAN_EXIT; // Fast exit from loop.
                    }
                }
            }
            return S_OK; // Return with success to continue walk.
        });

    // Note, ForEachFields() could return S_CAN_EXIT for fast exit.
    return SUCCEEDED(Status) ? S_OK : Status;
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
HRESULT TryParseGeneratedName(const WSTRING &mdName, WSTRING &wGeneratedName)
{
    if (mdName.length() <= 3)
    {
        return E_FAIL;
    }

    const WSTRING::size_type nameStartOffset = mdName.find(W('<'));
    if (mdName.find(W('<')) == WSTRING::npos)
    {
        return E_FAIL;
    }

    const WSTRING::size_type closeBracketOffset = mdName.find('>', nameStartOffset);
    if (closeBracketOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    wGeneratedName = mdName.substr(nameStartOffset + 1, closeBracketOffset - nameStartOffset - 1);
    return S_OK;
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                 std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef,
                                 DebugInfo *pDebugInfo, ICorDebugModule *pModule,
                                 const Evaluator::WalkStackVarsCallback &cb)
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

    return ForEachFields(pMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

            WSTRING mdName(nameLen, '\0');
            DWORD fieldAttr = 0;
            if (FAILED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                                &fieldAttr, nullptr, nullptr, nullptr, nullptr, nullptr)) ||
                (fieldAttr & fdStatic) != 0 ||
                (fieldAttr & fdLiteral) != 0)
            {
                return S_OK; // Return with success to continue walk.
            }
            // Remove null terminator that was included in the length
            if (!mdName.empty() && mdName.back() == '\0')
            {
                mdName.pop_back();
            }

            auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
            {
                // Get pValue again, since it could be neutered at eval call in `cb` on previous loop.
                trValue.Free();
                IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
                ToRelease<ICorDebugObjectValue> trObjValue;
                IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                return S_OK;
            };

            const GeneratedNameKind generatedNameKind = GetLocalOrFieldNameKind(mdName);
            if (generatedNameKind == GeneratedNameKind::DisplayClassLocalOrField)
            {
                ToRelease<ICorDebugValue> trDisplayClassValue;
                IfFailRet(getValue(&trDisplayClassValue, nullptr, false));
                IfFailRet(WalkGeneratedClassFields(pMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                                   pDebugInfo, pModule, cb));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from loop.
                }
            }
            else if (generatedNameKind == GeneratedNameKind::HoistedLocalField)
            {
                // Check that hoisted local is in scope.
                // Note: in case we have any issue, ignore this check and show the variable, since this is not a fatal error.
                int32_t index = 0;
                if (SUCCEEDED(TryParseSlotIndex(mdName, index)) && index >= 0 &&
                    !pDebugInfo->IsHoistedLocalInScope(pModule, methodDef, currentIlOffset, static_cast<uint32_t>(index)))
                {
                    return S_OK; // Return with success to continue walk.
                }

                if (usedNames.find(mdName) != usedNames.end())
                {
                    return S_OK; // Return with success to continue walk.
                }

                WSTRING wLocalName;
                if (FAILED(TryParseGeneratedName(mdName, wLocalName)))
                {
                    return S_OK; // Return with success to continue walk.
                }

                IfFailRet(cb(to_utf8(wLocalName.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from loop.
                }
                usedNames.insert(wLocalName);
            }
            // Ignore any other compiler generated fields, show only normal fields.
            else if (!IsSynthesizedLocalName(mdName) &&
                     usedNames.find(mdName) == usedNames.end())
            {
                IfFailRet(cb(to_utf8(mdName.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from loop.
                }
                usedNames.insert(mdName);
            }
            return S_OK; // Return with success to continue walk.
        });
}

HRESULT GetTypeGenerics(ICorDebugType *pType, std::vector<SigElementType> &typeGenerics)
{
    HRESULT Status = S_OK;
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

    return S_OK;
}

HRESULT FollowNestedFindType(ICorDebugThread *pThread, const std::string &methodClass,
                             std::vector<std::string> &identifiers, ICorDebugType **ppResultType)
{
    HRESULT Status = S_OK;

    std::vector<int> ranks;
    std::vector<std::string> classIdentifiers = EvalUtils::ParseType(methodClass, ranks);
    int nextClassIdentifier = 0;

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(EvalUtils::FindType(classIdentifiers, nextClassIdentifier, pThread, nullptr, nullptr, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.begin(), identifiers.end(), std::back_inserter(fullpath));

        nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(EvalUtils::FindType(fullpath, nextClassIdentifier, pThread, trModule, &trType)))
        {
            break;
        }

        assert(fullpath.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if (nextClassIdentifier == static_cast<int>(fullpath.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT GetFirstUserCodeEnclosingClass(IMetaDataImport *pMDImport, mdTypeDef typeDef, mdTypeDef &userTypeDef)
{
    HRESULT Status = S_OK;

    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, nullptr, 0, &nameLen, nullptr, nullptr));

        WSTRING mdName(nameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, mdName.data(), nameLen, nullptr, nullptr, nullptr));
        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        if (!IsSynthesizedLocalName(mdName))
        {
            userTypeDef = typeDef;
            break;
        }

        IfFailRet(pMDImport->GetNestedClassProps(typeDef, &typeDef));
    };

    return S_OK;
}

HRESULT WalkPrimaryConstructorParameterFields(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                              ICorDebugValue *pInputValue, std::unordered_set<WSTRING> &usedNames,
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

    return ForEachFields(pMDImport, typeDef, [&](mdFieldDef fieldDef) -> HRESULT
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

        WSTRING mdName(nameLen, '\0');
        DWORD fieldAttr = 0;
        if (FAILED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                            &fieldAttr, nullptr, nullptr, nullptr, nullptr, nullptr)) ||
            (fieldAttr & fdStatic) != 0 ||
            (fieldAttr & fdLiteral) != 0)
        {
            return S_OK; // Return with success to continue walk.
        }
        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        WSTRING wParameterName;
        if (GetLocalOrFieldNameKind(mdName) != GeneratedNameKind::PrimaryConstructorParameterField ||
            FAILED(TryParseGeneratedName(mdName, wParameterName)) ||
            usedNames.find(wParameterName) != usedNames.end())
        {
            return S_OK; // Return with success to continue walk.
        }

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
        {
            trValue.Free();
            IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, nullptr));
            ToRelease<ICorDebugObjectValue> trObjValue;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
            IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
            return S_OK;
        };

        IfFailRet(cb(to_utf8(wParameterName.c_str()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_CAN_EXIT; // Fast exit from loop.
        }
        usedNames.insert(wParameterName);

        return S_OK;
    });
}

// Helper function to remove leading and trailing whitespace from a std::string_view.
std::string_view TrimString(std::string_view str)
{
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void ParseTypeName(std::string_view proxyTypeName, std::vector<std::string> &typeNameParts, std::string &assemblyName)
{
    // 1. Separate the full type info from the assembly metadata using the first comma.
    auto firstComma = proxyTypeName.find(',');
    const std::string_view typePart = TrimString(proxyTypeName.substr(0, firstComma));
    const std::string_view remainder = (firstComma != std::string_view::npos) ? proxyTypeName.substr(firstComma + 1) : "";

    // 2. Extract the assembly name if it exists.
    if (!remainder.empty())
    {
        auto nextComma = remainder.find(',');
        const std::string_view assemblyPart = TrimString(remainder.substr(0, nextComma));

        // Per C# spec, the assembly name cannot start with properties like "Version=", "Culture=", etc.
        if (!assemblyPart.empty() &&
            assemblyPart.rfind("Version=", 0) != 0 &&
            assemblyPart.rfind("Culture=", 0) != 0 &&
            assemblyPart.rfind("PublicKeyToken=", 0) != 0)
        {
            assemblyName = std::string(assemblyPart);
        }
    }

    // 3. Split the type part by the '+' delimiter (nested classes).
    size_t start = 0;
    while (start < typePart.size())
    {
        auto plusPos = typePart.find('+', start);
        const std::string_view part = typePart.substr(start, plusPos - start);

        typeNameParts.emplace_back(TrimString(part));

        if (plusPos == std::string_view::npos)
        {
            break;
        }
        start = plusPos + 1;
    }
}

// Parses the generic arity (number after '`') and returns it as uint32_t.
// Returns 0 if there is no '`' character or if the parsing fails.
uint32_t ParseGenericArity(std::string_view typeName)
{
    // 1. Find the backtick character '`'.
    auto backtickPos = typeName.find('`');
    if (backtickPos == std::string_view::npos)
    {
        return 0; // Not a generic type
    }

    // 2. Extract the substring representing the number.
    const std::string_view numberPart = typeName.substr(backtickPos + 1);
    if (numberPart.empty())
    {
        return 0; // Empty after backtick, e.g., "MyClass`"
    }

    // 3. Fast and safe string-to-number conversion using C++17 std::from_chars.
    uint32_t count = 0;
    auto [ptr, ec] = std::from_chars(numberPart.data(), numberPart.data() + numberPart.size(), count);

    // If conversion succeeded, return the count; otherwise, return 0.
    if (ec == std::errc{})
    {
        return count;
    }

    return 0;
}

void GetParameterClassNames(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, std::unordered_set<std::string> &parameterTypeNames)
{
    // Add the class name itself.
    std::string typeName;
    TypePrinter::FullyQualifiedNameForTypeDef(currentTypeDef, pMDImport, typeName);
    parameterTypeNames.emplace(std::move(typeName));

    // Add all interface names.
    HCORENUM hEnum = nullptr;
    mdInterfaceImpl ifaceImpl = mdInterfaceImplNil;
    ULONG cImpls = 0;
    while (SUCCEEDED(pMDImport->EnumInterfaceImpls(&hEnum, currentTypeDef, &ifaceImpl, 1, &cImpls)) && cImpls != 0)
    {
        mdTypeDef tkClass = mdTypeDefNil;
        mdToken tkIface = mdTokenNil;
        if (FAILED(pMDImport->GetInterfaceImplProps(ifaceImpl, &tkClass, &tkIface)))
        {
            continue;
        }

        std::string typeName;
        TypePrinter::FullyQualifiedNameForTypeByToken(tkIface, pMDImport, typeName);
        parameterTypeNames.emplace(std::move(typeName));
    }
    pMDImport->CloseEnum(hEnum);
}

HRESULT GetConstructorFunction(ICorDebugModule *pModule, IMetaDataImport *pMDImport, mdTypeDef typeDef,
                               const std::unordered_set<std::string> &parameterTypeNames,
                               mdMethodDef &constrMethodDef, ICorDebugFunction **ppConstrFunction)
{
    constrMethodDef = mdMethodDefNil;

    HRESULT Status = S_OK;
    ULONG numMethods = 0;
    HCORENUM mEnum = nullptr;
    mdMethodDef enumMethodDef = mdMethodDefNil;
    while (S_OK == pMDImport->EnumMethodsWithName(&mEnum, typeDef, W(".ctor"), &enumMethodDef, 1, &numMethods) && numMethods == 1)
    {
        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        if (FAILED(pMDImport->GetMethodProps(enumMethodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
        {
            continue;
        }

        if ((methodAttr & mdMemberAccessMask) != mdPublic ||
            (methodAttr & mdStatic) != 0U ||
            (methodAttr & mdRTSpecialName) == 0U)
        {
            continue;
        }

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        if (FAILED(ParseMethodSig(pMDImport, enumMethodDef, pSig, pSig + cbSig, returnElementType, argElementTypes)))
        {
            continue;
        }

        if (argElementTypes.size() != 1 ||
            parameterTypeNames.find(argElementTypes.front().typeName) == parameterTypeNames.end())
        {
            continue;
        }

        constrMethodDef = enumMethodDef;
        break;
    }
    pMDImport->CloseEnum(mEnum);

    if (constrMethodDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    IfFailRet(pModule->GetFunctionFromToken(constrMethodDef, ppConstrFunction));
    return S_OK;
}

HRESULT GetConstructorTypeParams(ICorDebugThread *pThread, ICorDebugType *pType, uint32_t enclosingTypesParamCount,
                                 std::vector<ToRelease<ICorDebugType>> &trTypeParams)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    std::vector<ToRelease<ICorDebugType>> trCurrentTypes;
    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ICorDebugType *curType = nullptr;
        ULONG fetched = 0;
        while (SUCCEEDED(Status = trTypeEnum->Next(1, &curType, &fetched)) && fetched == 1)
        {
            trCurrentTypes.emplace_back(curType);
        }
    }

    // Add System.Object type parameters for enclosing classes.
    ToRelease<ICorDebugType> trObjectType;
    for (uint32_t j = 0; j < enclosingTypesParamCount; j++)
    {
        if (trObjectType == nullptr)
        {
            ToRelease<ICorDebugValue> trNullObjectValue;
            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, nullptr, &trNullObjectValue));
            ToRelease<ICorDebugValue2> trNullObjectValue2;
            IfFailRet(trNullObjectValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trNullObjectValue2)));
            IfFailRet(trNullObjectValue2->GetExactType(&trObjectType));
        }
        trObjectType->AddRef();
        trTypeParams.emplace_back(trObjectType.GetPtr());
    }

    // Add the proxy class type parameters.
    for (auto &trType : trCurrentTypes)
    {
        trTypeParams.emplace_back(trType.Detach());
    }

    return S_OK;
}

HRESULT DetectDebuggerTypeProxyAttribute(ICorDebugType *pType, std::string &proxyTypeName, mdTypeDef &proxyAttrTypeDef, ToRelease<ICorDebugModule> &trProxyAttrModule)
{
    pType->AddRef();
    ToRelease<ICorDebugType> trTestType(pType);
    while (true)
    {
        trProxyAttrModule.Free();
        ToRelease<ICorDebugClass> trClass;
        mdTypeDef attrTypeDef = mdTypeDefNil;
        ToRelease<IUnknown> trUnknown;
        ToRelease<IMetaDataImport> trMDImport;
        if (FAILED(trTestType->GetClass(&trClass)) ||
            FAILED(trClass->GetModule(&trProxyAttrModule)) ||
            FAILED(trClass->GetToken(&attrTypeDef)) ||
            FAILED(trProxyAttrModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown)) ||
            FAILED(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport))))
        {
            break;
        }

        if (HasDebuggerTypeProxyAttribute(trMDImport, attrTypeDef, proxyTypeName))
        {
            proxyAttrTypeDef = attrTypeDef;
            break;
        }

        ToRelease<IMetaDataAssemblyImport> trAssemblyImport;
        mdAssembly assemblyToken = mdAssemblyNil;
        if (SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&trAssemblyImport))) &&
            SUCCEEDED(trAssemblyImport->GetAssemblyFromScope(&assemblyToken)))
        {
            std::string detectTypeName;
            if (SUCCEEDED(TypePrinter::FullyQualifiedNameForTypeDef(attrTypeDef, trMDImport, detectTypeName)) &&
                HasAssemblyDebuggerTypeProxyAttribute(trMDImport, assemblyToken, detectTypeName, proxyTypeName))
            {
                proxyAttrTypeDef = attrTypeDef;
                break;
            }
        }

        ToRelease<ICorDebugType> trBaseType;
        if (FAILED(trTestType->GetBase(&trBaseType)) || trBaseType == nullptr)
        {
            break;
        }
        trTestType.Free();
        trTestType = trBaseType.Detach();
    }

    return proxyAttrTypeDef != mdTypeDefNil && !proxyTypeName.empty() && trProxyAttrModule != nullptr ? S_OK : E_FAIL;
}

} // unnamed namespace

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

#ifdef BIT64
    assert(indexes.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
    return trArrayVal->GetElement(static_cast<uint32_t>(indexes.size()), indexes.data(), ppResultValue);
}

HRESULT Evaluator::WalkMethods(ICorDebugValue *pInputTypeValue, bool walkBaseType, const WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pInputTypeValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugType> trResultType;

    return WalkMethods(trType, walkBaseType, &trResultType, cb);
}

HRESULT Evaluator::WalkMethods(ICorDebugType *pInputType, bool walkBaseType, ICorDebugType **ppResultType,
                               const Evaluator::WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    pInputType->AddRef();
    ToRelease<ICorDebugType> trInputType(pInputType);

    std::vector<SigElementType> typeGenerics;
    IfFailRet(GetTypeGenerics(pInputType, typeGenerics));

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

        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(trMDImport->EnumMethods(&fEnum, currentTypeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            mdTypeDef memTypeDef = mdTypeDefNil;
            std::vector<WCHAR> szFunctionName(nameLen, '\0');
            DWORD methodAttr = 0;
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), nameLen, nullptr,
                                                  &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes)))
            {
                continue;
            }

            if (FAILED(ApplyTypeGenerics(typeGenerics, returnElementType)))
            {
                continue;
            }

            bool applyFailed = false;
            for (auto &argType : argElementTypes)
            {
                if (FAILED(ApplyTypeGenerics(typeGenerics, argType)))
                {
                    applyFailed = true;
                }
            }
            if (applyFailed)
            {
                continue;
            }

            const bool isStatic = ((methodAttr & mdStatic) != 0U);

            auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
            {
                return trModule->GetFunctionFromToken(methodDef, ppResultFunction);
            };

            IfFailRet(cb(isStatic, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, getFunction));
            if (Status == S_CAN_EXIT)
            {
                if (ppResultType != nullptr)
                {
                    *ppResultType = trInputType.Detach();
                }
                trMDImport->CloseEnum(fEnum);
                return S_OK;
            }
        }
        trMDImport->CloseEnum(fEnum);

        ToRelease<ICorDebugType> trBaseType;
        if (walkBaseType &&
            SUCCEEDED(trInputType->GetBase(&trBaseType)) && trBaseType != nullptr)
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
            IfFailRet((*getValue)(&trPrevValue, nullptr, false));
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
        return m_sharedEvalHelpers->EvalFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetPtr(),
                                                 nullptr, trValue.GetRef(), 1, nullptr);
    }
    else
    {
        std::array<ICorDebugValue *, 2> argsValue{setterData->trThisValue, trValue};
        return m_sharedEvalHelpers->EvalFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetPtr(),
                                                 nullptr, argsValue.data(), 2, nullptr);
    }
}

HRESULT Evaluator::GetStaticField(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugType *pType,
                                  mdFieldDef fieldDef, ICorDebugValue **ppResultValue)
{
    if (pThread == nullptr)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));

    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    // Detect if static field is initialized (static constructor .cctor called).
    // We read the MethodTable's initialization flag directly from memory.
    // The COR_TYPEID.token1 is the MethodTable address.
    // MethodTable has m_pAuxiliaryData pointer, and MethodTableAuxiliaryData
    // has m_dwFlags where bit 0 (enum_flag_Initialized = 0x0001) indicates
    // whether the class constructor has run.
    static constexpr DWORD enum_flag_Initialized = 0x0001;

    bool isClassInitialized = true; // Assume initialized by default

    // Get the MethodTable address via ICorDebugType2::GetTypeID.
    ToRelease<ICorDebugType2> trType2;
    COR_TYPEID typeID = {0, 0};
    if (SUCCEEDED(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2))) &&
        SUCCEEDED(trType2->GetTypeID(&typeID)) && typeID.token1 != 0)
    {
        // typeID.token1 is the MethodTable address.
        const CORDB_ADDRESS methodTableAddr = typeID.token1;

        ToRelease<ICorDebugProcess> trProcess;
        IfFailRet(pThread->GetProcess(&trProcess));
        if (trProcess == nullptr)
        {
            return E_FAIL;
        }

        // Read the MethodTable to get m_pAuxiliaryData pointer
        // The offset of m_pAuxiliaryData in MethodTable varies by platform
        // We use the fact that m_dwFlags is at offset 0 in MethodTableAuxiliaryData
        // and the Initialized flag is bit 0

        // Read enough of MethodTable to get to m_pAuxiliaryData
        // MethodTable layout (from runtime sources):
        // - m_dwFlags (DWORD) at offset 0
        // - m_BaseSize (DWORD) at offset 4
        // - m_dwFlags2 (DWORD) at offset 8
        // - m_wNumVirtuals (WORD) at offset 12
        // - m_wNumInterfaces (WORD) at offset 14
        // - m_pParentMethodTable (pointer) at offset 16
        // - m_pModule (pointer) at offset 16 + sizeof(pointer)
        // - m_pAuxiliaryData (pointer) at offset 16 + 2*sizeof(pointer)
        static constexpr size_t auxDataOffset = (sizeof(DWORD) * 3) + (sizeof(WORD) * 2) + (sizeof(void *) * 2);
        static constexpr size_t readSize = auxDataOffset + sizeof(void *);
        std::array<BYTE, readSize> buffer{0};
        SIZE_T bytesRead = 0;

        if (SUCCEEDED(trProcess->ReadMemory(methodTableAddr, readSize, buffer.data(), &bytesRead)) && bytesRead >= readSize)
        {
            // Get the auxiliary data pointer.
            const CORDB_ADDRESS auxDataAddr = *reinterpret_cast<const CORDB_ADDRESS*>(buffer.data() + auxDataOffset);
            if (auxDataAddr != 0)
            {
                // Read m_dwFlags from MethodTableAuxiliaryData (at offset 0).
                DWORD auxFlags = 0;
                if (SUCCEEDED(trProcess->ReadMemory(auxDataAddr, sizeof(DWORD), reinterpret_cast<BYTE*>(&auxFlags), &bytesRead)))
                {
                    isClassInitialized = (auxFlags & enum_flag_Initialized) != 0;
                }
            }
        }
    }

    // The class should already be initialized at this point. If not, force the
    // static constructor execution to provide a second chance and proper error handling.
    if (!isClassInitialized)
    {
        IfFailRet(m_sharedEvalHelpers->CreateTypeObjectStaticConstructor(pThread, pType, nullptr, false));
    }

    IfFailRet(pType->GetStaticFieldValue(fieldDef, trFrame, ppResultValue));

    return S_OK;
}

HRESULT Evaluator::GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugModule *pAttrModule,
                                             ICorDebugValue *pFrontValue, ICorDebugType *pType, mdTypeDef currentTypeDef,
                                             mdTypeDef proxyAttrTypeDef, const std::string &proxyTypeName, ICorDebugValue **ppTypeProxyValue)
{
    HRESULT Status = S_OK;

    std::vector<std::string> proxyTypeNameParts;
    std::string assemblyName;
    ParseTypeName(proxyTypeName, proxyTypeNameParts, assemblyName);

    pAttrModule->AddRef();
    ToRelease<ICorDebugModule> trProxyTypeModule(pAttrModule);
    if (!assemblyName.empty())
    {
        IfFailRet(Modules::ForEachModule(pThread,
            [&](ICorDebugModule *pTestModule) -> HRESULT
            {
                uint32_t nameLen = 0;
                if (FAILED(pTestModule->GetName(0, &nameLen, nullptr)))
                {
                    return S_OK;
                }

                std::vector<WCHAR> wModName(nameLen, '\0');
                if (FAILED(pTestModule->GetName(nameLen, nullptr, wModName.data())))
                {
                    return S_OK;
                }

                if (RemoveExtension(GetBasename(to_utf8(wModName.data()))) == assemblyName)
                {
                    trProxyTypeModule.Free();
                    pTestModule->AddRef();
                    trProxyTypeModule = pTestModule;
                    return S_CAN_EXIT;
                }

                return S_OK;
            }));
    }

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trProxyTypeModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    if (proxyTypeNameParts.size() == 1 && assemblyName.empty() &&
        std::count(proxyTypeNameParts.front().begin(), proxyTypeNameParts.front().end(), '.') == 0)
    {
        const WSTRING wProxyTypeName = to_utf16(proxyTypeNameParts.front());
        IfFailRet(trMDImport->FindTypeDefByName(wProxyTypeName.c_str(), proxyAttrTypeDef, &typeDef));
    }

    if (typeDef == mdTypeDefNil)
    {
        mdToken enclosingClass = mdTypeDefNil;
        for (const auto &namePart : proxyTypeNameParts)
        {
            const WSTRING wNamePart = to_utf16(namePart);
            IfFailRet(trMDImport->FindTypeDefByName(wNamePart.c_str(), enclosingClass, &typeDef));
            enclosingClass = typeDef;
        }
    }

    // Get the proper fully-qualified proxy type name.
    std::string fullProxyTypeName;
    IfFailRet(TypePrinter::FullyQualifiedNameForTypeDef(typeDef, trMDImport, fullProxyTypeName));
    proxyTypeNameParts.clear();
    std::string tmp;
    ParseTypeName(fullProxyTypeName, proxyTypeNameParts, tmp);

    std::unordered_set<std::string> parameterTypeNames;
    GetParameterClassNames(trMDImport, proxyAttrTypeDef, parameterTypeNames);

    mdMethodDef constrMethodDef = mdMethodDefNil;
    ToRelease<ICorDebugFunction> trConstrFunction;
    IfFailRet(GetConstructorFunction(trProxyTypeModule, trMDImport, typeDef, parameterTypeNames, constrMethodDef, &trConstrFunction));

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    uint32_t enclosingTypesParamCount = 0; // type parameters for enclosing classes
    if (proxyTypeNameParts.size() > 1)
    {
        for (std::size_t i = 0; i < proxyTypeNameParts.size() - 1; i++)
        {
            for (uint32_t j = 0; j < ParseGenericArity(proxyTypeNameParts.at(i)); j++)
            {
                enclosingTypesParamCount++;
            }
        }
    }
    IfFailRet(GetConstructorTypeParams(pThread, pType, enclosingTypesParamCount, trTypeParams));

    IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppTypeProxyValue,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->NewParameterizedObject(trConstrFunction, static_cast<uint32_t>(trTypeParams.size()),
                                                      reinterpret_cast<ICorDebugType **>(trTypeParams.data()), 1, &pFrontValue));
            return S_OK;
        }));

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));
    CORDB_ADDRESS proxyTypeModAddress = 0;
    IfFailRet(trProxyTypeModule->GetBaseAddress(&proxyTypeModAddress));

    const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

    m_debuggerTypeProxyCache[modAddress].emplace(currentTypeDef, DebuggerTypeProxyCache{proxyTypeModAddress, constrMethodDef, enclosingTypesParamCount});

    if (m_debuggerTypeProxyModuleCache.find(proxyTypeModAddress) == m_debuggerTypeProxyModuleCache.end())
    {
        m_debuggerTypeProxyModuleCache.emplace(proxyTypeModAddress, trProxyTypeModule.Detach());
    }

    return S_OK;
}

HRESULT Evaluator::GetCachedDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue, ICorDebugType *pType,
                                                   mdTypeDef currentTypeDef, bool &typeChecked, ICorDebugValue **ppTypeProxyValue)
{
    typeChecked = false;

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::unique_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

    auto findCheckedModule = m_debuggerTypeProxyCheckedTypes.find(modAddress);
    if (findCheckedModule == m_debuggerTypeProxyCheckedTypes.end())
    {
        m_debuggerTypeProxyCheckedTypes.emplace(modAddress, std::unordered_set<mdTypeDef>{});
        m_debuggerTypeProxyCheckedTypes.at(modAddress).emplace(currentTypeDef);
        return E_FAIL;
    }

    auto findCheckedType = findCheckedModule->second.find(currentTypeDef);
    if (findCheckedType == findCheckedModule->second.end())
    {
        findCheckedModule->second.emplace(currentTypeDef);
        return E_FAIL;
    }

    typeChecked = true;

    auto findCacheByModule = m_debuggerTypeProxyCache.find(modAddress);
    if (findCacheByModule == m_debuggerTypeProxyCache.end())
    {
        return E_FAIL;
    }

    auto findCache = findCacheByModule->second.find(currentTypeDef);
    if (findCache == findCacheByModule->second.end())
    {
        return E_FAIL;
    }

    const DebuggerTypeProxyCache &proxyCache = findCache->second;
    ICorDebugModule *pProxyTypeModule = m_debuggerTypeProxyModuleCache.at(proxyCache.modAddress);

    ToRelease<ICorDebugFunction> trConstrFunction;
    IfFailRet(pProxyTypeModule->GetFunctionFromToken(proxyCache.methodDef, &trConstrFunction));

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    IfFailRet(GetConstructorTypeParams(pThread, pType, proxyCache.enclosingTypesParamCount, trTypeParams));

    lock.unlock();

    IfFailRet(Status = m_sharedEvalWaiter->WaitEvalResult(pThread, ppTypeProxyValue,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->NewParameterizedObject(trConstrFunction, static_cast<uint32_t>(trTypeParams.size()),
                                                      reinterpret_cast<ICorDebugType **>(trTypeParams.data()), 1, &pFrontValue));
            return S_OK;
        }));

    return S_OK;
}

HRESULT Evaluator::WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                               bool provideSetterData, const WalkMembersCallback &cb)
{
    HRESULT Status = S_OK;

    struct WalkValue
    {
        ToRelease<ICorDebugValue> trValue;
        bool isTypeProxyValue = false;

        WalkValue(ICorDebugValue *pValue, bool isTypeProxyValue_)
            : trValue(pValue),
              isTypeProxyValue(isTypeProxyValue_)
        {
        }
    };

    // Queue of fields/properties to process. Includes elements with
    // DebuggerBrowsableState.RootHidden to unwrap their members during the walk.
    std::list<WalkValue> trWalkQueue;
    pInputValue->AddRef();
    trWalkQueue.emplace_back(pInputValue, false);

    auto walkNext = [&](ICorDebugValue *pFrontValue, bool isTypeProxyValue) -> HRESULT
    {
        BOOL isNull = FALSE;
        ToRelease<ICorDebugValue> trValue;
        IfFailRet(DereferenceAndUnboxValue(pFrontValue, &trValue, &isNull));
        if (trValue == nullptr)
        {
            return isNull == TRUE ? S_OK : E_FAIL;
        }

        CorElementType inputCorType = ELEMENT_TYPE_MAX;
        IfFailRet(pFrontValue->GetType(&inputCorType));
        if (inputCorType == ELEMENT_TYPE_PTR)
        {
            auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
            {
                trValue->AddRef();
                *ppResultValue = trValue;
                return S_OK;
            };

            IfFailRet(cb(nullptr, false, "", getValue, nullptr));
            // Note, cb could return S_CAN_EXIT for fast exit.
            return S_OK;
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
            BOOL hasBaseIndices = FALSE;
            if (SUCCEEDED(trArrayValue->HasBaseIndicies(&hasBaseIndices)) && (hasBaseIndices == TRUE))
            {
                IfFailRet(trArrayValue->GetBaseIndicies(nRank, base.data()));
            }

            std::vector<uint32_t> ind(nRank, 0);

            for (uint32_t i = 0; i < cElements; ++i)
            {
                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
                {
                    IfFailRet(trArrayValue->GetElementAtPosition(i, ppResultValue));
                    return S_OK;
                };

                IfFailRet(cb(nullptr, false, "[" + IndicesToStr(ind, base) + "]", getValue, nullptr));
                if (Status == S_CAN_EXIT)
                {
                    return S_OK;
                }
                IncIndices(dims, ind);
            }

            return S_OK;
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));
        if (trType == nullptr)
        {
            return E_FAIL;
        }

        while (trType != nullptr)
        {
            std::string className;
            TypePrinter::GetTypeOfValue(trType, className);
            if (className == "decimal")
            {
                return S_OK;
            }

            if (className.back() == '?') // System.Nullable<T>
            {
                ToRelease<ICorDebugValue> trValueValue;
                ToRelease<ICorDebugValue> trHasValueValue;
                IfFailRet(GetNullableValue(trValue, &trValueValue, &trHasValueValue));

                uint8_t boolValue = 0;
                IfFailRet(GetIntegralValue(trHasValueValue, boolValue));

                if (boolValue == 1) // TRUE
                {
                    trValue.Free();
                    trValue = trValueValue.Detach();
                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    trType.Free();
                    IfFailRet(trValue2->GetExactType(&trType));

                    continue;
                }

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

            if ((GetEvalFlags() & EVAL_SHOWRAWVALUES) == 0U && isNull == FALSE && !isTypeProxyValue &&
                (corElemType == ELEMENT_TYPE_CLASS || corElemType == ELEMENT_TYPE_VALUETYPE))
            {
                bool typeChecked = false;
                ToRelease<ICorDebugValue> trTypeProxyValue;
                if (SUCCEEDED(GetCachedDebuggerTypeProxyValue(pThread, trModule, pFrontValue, trType,
                                                              currentTypeDef, typeChecked, &trTypeProxyValue)))
                {
                    trWalkQueue.emplace_front(trTypeProxyValue.Detach(), true);
                    return S_OK;
                }

                std::string proxyTypeName;
                mdTypeDef proxyAttrTypeDef = mdTypeDefNil;
                ToRelease<ICorDebugModule> trProxyAttrModule;
                if (!typeChecked &&
                    SUCCEEDED(DetectDebuggerTypeProxyAttribute(trType, proxyTypeName, proxyAttrTypeDef, trProxyAttrModule)))
                {
                    CORDB_ADDRESS modAddress = 0;
                    if (SUCCEEDED(GetDebuggerTypeProxyValue(pThread, trModule, trProxyAttrModule, pFrontValue, trType, currentTypeDef,
                                                            proxyAttrTypeDef, proxyTypeName, &trTypeProxyValue)))
                    {
                        trWalkQueue.emplace_front(trTypeProxyValue.Detach(), true);
                        return S_OK;
                    }
                    else if (SUCCEEDED(trModule->GetBaseAddress(&modAddress)))
                    {
                        const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);
                        // Could be an issue with thread state, reset checked status, try next time.
                        m_debuggerTypeProxyCheckedTypes.at(modAddress).erase(currentTypeDef);
                    }
                }
            }

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            IfFailRet(ForEachFields(trMDImport, currentTypeDef,
                [&](mdFieldDef fieldDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = (GetEvalFlags() & EVAL_SHOWRAWVALUES) == 0U ?
                                                                  GetDebuggerBrowsableAttributeState(trMDImport, fieldDef) :
                                                                  DebuggerBrowsableState::Collapsed;
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return with success to continue walk.
                    }

                    ULONG nameLen = 0;
                    DWORD fieldAttr = 0;
                    IfFailRet(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, &fieldAttr,
                                                        nullptr, nullptr, nullptr, nullptr, nullptr));

                    if (isTypeProxyValue &&
                        (fieldAttr & fdFieldAccessMask) != fdPublic)
                    {
                        return S_OK; // Return with success to continue walk.
                    }

                    WSTRING mdName(nameLen, '\0');
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    UVCP_CONSTANT pRawValue = nullptr;
                    ULONG rawValueLength = 0;
                    if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, nullptr,
                                                            &pSig, &cbSig, nullptr, &pRawValue, &rawValueLength)))
                    {
                        // Remove null terminator that was included in the length
                        if (!mdName.empty() && mdName.back() == '\0')
                        {
                            mdName.pop_back();
                        }

                        // Prevent access to internal compiler added fields (without visible name).
                        // Should be accessed by debugger routine only and hidden from user/ide.
                        // More about compiler generated names in Roslyn sources:
                        // https://github.com/dotnet/roslyn/blob/315c2e149ba7889b0937d872274c33fcbfe9af5f/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNames.cs
                        // Note, uncontrolled access to internal compiler added field or its properties may break debugger work.
                        if (IsSynthesizedLocalName(mdName))
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        const bool isStatic = (fieldAttr & fdStatic);
                        if (isNull == TRUE && !isStatic)
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        const std::string name = to_utf8(mdName.c_str());

                        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
                        {
                            if (fieldAttr & fdLiteral)
                            {
                                IfFailRet(m_sharedEvalHelpers->CreateLiteralFieldValue(pThread, pSig, pSig + cbSig, pRawValue,
                                                                                       rawValueLength, ppResultValue));
                            }
                            else if (fieldAttr & fdStatic)
                            {
                                IfFailRet(GetStaticField(pThread, frameLevel, trType, fieldDef, ppResultValue));
                            }
                            else
                            {
                                // Re-acquire trValue from pFrontValue, since it could be neutered by eval call in `cb` on previous iteration.
                                trValue.Free();
                                IfFailRet(DereferenceAndUnboxValue(pFrontValue, &trValue, &isNull));
                                ToRelease<ICorDebugObjectValue> trObjValue;
                                IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                            }

                            return S_OK;
                        };

                        if (browsableState == DebuggerBrowsableState::RootHidden)
                        {
                            ToRelease<ICorDebugValue> trResultValue;
                            if (SUCCEEDED(getValue(&trResultValue, nullptr, false)))
                            {
                                trWalkQueue.emplace_back(trResultValue.Detach(), false);
                            }
                            return S_OK; // Return with success to continue walk.
                        }

                        IfFailRet(cb(trType, isStatic, name, getValue, nullptr));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from loop.
                        }
                    }
                    return S_OK; // Return with success to continue walk.
                }));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }
            Status = ForEachProperties(trMDImport, currentTypeDef,
                [&](mdProperty propertyDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = (GetEvalFlags() & EVAL_SHOWRAWVALUES) == 0U ?
                                                                  GetDebuggerBrowsableAttributeState(trMDImport, propertyDef) :
                                                                  DebuggerBrowsableState::Collapsed;
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return with success to continue walk.
                    }

                    ULONG propertyNameLen = 0;
                    IfFailRet(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, &propertyNameLen,
                                                           nullptr, nullptr, nullptr, nullptr, nullptr,
                                                           nullptr, nullptr, nullptr, nullptr, 0, nullptr));

                    mdMethodDef mdGetter = mdMethodDefNil;
                    mdMethodDef mdSetter = mdMethodDefNil;
                    std::vector<WCHAR> propertyName(propertyNameLen, '\0');
                    if (SUCCEEDED(trMDImport->GetPropertyProps(propertyDef, nullptr, propertyName.data(), propertyNameLen,
                                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                               nullptr, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
                    {
                        DWORD getterAttr = 0;
                        if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                              nullptr, nullptr, nullptr, nullptr)))
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        if (isTypeProxyValue &&
                            (getterAttr & mdMemberAccessMask) != mdPublic)
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        bool isStatic = (getterAttr & mdStatic);
                        if (isNull == TRUE && !isStatic)
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        const std::string name = to_utf8(propertyName.data());

                        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool ignoreEvalFlags) -> HRESULT
                        {
                            if (pThread == nullptr)
                            {
                                return E_FAIL;
                            }

                            ToRelease<ICorDebugFunction> trFunc;
                            IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                            return m_sharedEvalHelpers->EvalFunction(pThread, trFunc, trType.GetPtr(), nullptr,
                                                                     isStatic ? nullptr : &pFrontValue, isStatic ? 0 : 1,
                                                                     ppResultValue, ignoreEvalFlags);
                        };

                        if (browsableState == DebuggerBrowsableState::RootHidden)
                        {
                            ToRelease<ICorDebugValue> trResultValue;
                            if (SUCCEEDED(getValue(&trResultValue, nullptr, false)))
                            {
                                trWalkQueue.emplace_back(trResultValue.Detach(), false);
                            }
                            return S_OK; // Return with success to continue walk.
                        }

                        if (provideSetterData)
                        {
                            ToRelease<ICorDebugFunction> trFuncSetter;
                            if (FAILED(trModule->GetFunctionFromToken(mdSetter, &trFuncSetter)))
                            {
                                trFuncSetter.Free();
                            }
                            Evaluator::SetterData setterData(isStatic ? nullptr : pFrontValue, trType, trFuncSetter);
                            IfFailRet(cb(trType, isStatic, name, getValue, &setterData));
                            if (Status == S_CAN_EXIT)
                            {
                                return S_CAN_EXIT; // Fast exit from loop.
                            }
                        }
                        else
                        {
                            IfFailRet(cb(trType, isStatic, name, getValue, nullptr));
                            if (Status == S_CAN_EXIT)
                            {
                                return S_CAN_EXIT; // Fast exit from loop.
                            }
                        }
                    }
                    return S_OK; // Return with success to continue walk.
                });
            // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
            IfFailRet(Status);
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }

            std::string baseTypeName;
            ToRelease<ICorDebugType> trBaseType;
            if (SUCCEEDED(trType->GetBase(&trBaseType)) && trBaseType != nullptr &&
                SUCCEEDED(TypePrinter::GetTypeOfValue(trBaseType, baseTypeName)))
            {
                trType.Free();

                if (baseTypeName == "System.Enum")
                {
                    return S_OK;
                }
                else if (baseTypeName != "object" && baseTypeName != "System.Object" && baseTypeName != "System.ValueType")
                {
                    if (pThread != nullptr)
                    {
                        m_sharedEvalHelpers->CreateTypeObjectStaticConstructor(pThread, trBaseType, nullptr, false);
                    }
                    // Add fields of base class.
                    trType = trBaseType.Detach();
                }
            }
            else
            {
                trType.Free();
            }
        }

        return S_OK;
    };

    while (!trWalkQueue.empty())
    {
        const ToRelease<ICorDebugValue> trFrontValue(trWalkQueue.front().trValue.Detach());
        const bool isTypeProxyValue = trWalkQueue.front().isTypeProxyValue;
        trWalkQueue.pop_front();

        IfFailRet(walkNext(trFrontValue, isTypeProxyValue));
    }

    return S_OK;
}

// Note: This method returns class name, not type name (will not provide generic initialization types if any).
HRESULT Evaluator::GetMethodClass(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &methodClass, bool &haveThis)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
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

    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &szMethodLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    DWORD methodAttr = 0;
    WSTRING szMethod(szMethodLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), szMethodLen, nullptr,
                                         &methodAttr, nullptr, nullptr, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!szMethod.empty() && szMethod.back() == '\0')
    {
        szMethod.pop_back();
    }

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
    IfFailRet(GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
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
    mdTypeDef userTypeDef = mdTypeDefNil;
    IfFailRet(GetFirstUserCodeEnclosingClass(trMDImport, typeDef, userTypeDef));

    return TypePrinter::NameForTypeDef(userTypeDef, trMDImport, methodClass, nullptr);
}

HRESULT Evaluator::WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
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

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    uint32_t currentIlOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(trILFrame->GetIP(&currentIlOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

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
    // 2. "real" arguments.
    // 3. "real" local variables.
    // 4. async/lambda object fields.

    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &szMethodLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    DWORD methodAttr = 0;
    WSTRING szMethod(szMethodLen, '\0');
    PCCOR_SIGNATURE pSig = nullptr;
    ULONG cbSig = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), szMethodLen, nullptr,
                                         &methodAttr, &pSig, &cbSig, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!szMethod.empty() && szMethod.back() == '\0')
    {
        szMethod.pop_back();
    }

    GeneratedCodeKind generatedCodeKind = GeneratedCodeKind::Normal;
    ToRelease<ICorDebugValue> trCurrentThis; // Current This. Note, in case async method or lambda - this is special object (non-user's "this").
    ToRelease<ICorDebugValue> trUserThis;
    ToRelease<ICorDebugClass> trUserThisClass;
    mdTypeDef userThisTypeDef = mdTypeDefNil;
    // In case this is static method, this is not async/lambda case for sure.
    if ((methodAttr & mdStatic) == 0)
    {
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trFunction->GetClass(&trClass));
        mdTypeDef typeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&typeDef));
        IfFailRet(GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
        Status = trILFrame->GetArgument(0, &trCurrentThis);
        if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE)
        {
            auto getValue = [&](ICorDebugValue **, std::string *fallbackTypeName, bool) -> HRESULT
            {
                std::string methodName;
                TypePrinter::GetTypeAndMethodName(trFrame, m_sharedDebugInfo.get(), *fallbackTypeName, methodName);
                return CORDBG_E_IL_VAR_NOT_AVAILABLE;
            };

            IfFailRet(cb("this", getValue));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
            // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
            trFrame.Free();
            trILFrame.Free();
        }
        else if (FAILED(Status))
        {
            return Status;
        }
        else
        {
            if (generatedCodeKind == GeneratedCodeKind::Normal)
            {
                trCurrentThis->AddRef();
                trUserThis = trCurrentThis.GetPtr();
                trClass->AddRef();
                trUserThisClass = trClass.GetPtr();
                userThisTypeDef = typeDef;
            }
            else
            {
                // Check if we have real This value (that should be stored in ThisProxyField).
                IfFailRet(FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
                if (trUserThis != nullptr)
                {
                    IfFailRet(GetFirstUserCodeEnclosingClass(trMDImport, typeDef, userThisTypeDef));
                    IfFailRet(trModule->GetClassFromToken(userThisTypeDef, &trUserThisClass));
                }
            }

            if (trUserThis != nullptr)
            {
                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
                {
                    trUserThis->AddRef();
                    *ppResultValue = trUserThis;
                    return S_OK;
                };

                IfFailRet(cb("this", getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_OK;
                }
                // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
                trFrame.Free();
                trILFrame.Free();
            }
        }
    }

    // Lambda could duplicate arguments into display class local object. Make sure we call "cb" only once for unique name.
    // Note, we don't use usedNames with 'this' related code above, since it has logic "find first and return".
    // At the same time, all code below ignores 'this' argument/field check.
    std::unordered_set<WSTRING> usedNames;

    for (ULONG i = (methodAttr & mdStatic) == 0 ? 1 : 0; i < cArguments; i++)
    {
        // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
        // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
        // Note, IMetaDataImport::GetParamForMethodIndex() don't include "this", but ICorDebugILFrame::GetArgument() do. This is why we have different logic here.
        ULONG paramNameLen = 0;
        mdParamDef paramDef = mdParamDefNil;
        const ULONG idx = ((methodAttr & mdStatic) == 0) ? i : (i + 1);
        if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
            FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0, &paramNameLen,
                                             nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        WSTRING wParamName(paramNameLen, '\0');
        if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }
        // Remove null terminator that was included in the length
        if (!wParamName.empty() && wParamName.back() == '\0')
        {
            wParamName.pop_back();
        }

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *fallbackTypeName, bool) -> HRESULT
        {
            if (trFrame == nullptr) // Forced to update trFrame/trILFrame.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
                if (trFrame == nullptr)
                {
                    return E_FAIL;
                }
                IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
            }

            Status = trILFrame->GetArgument(i, ppResultValue);
            if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE && fallbackTypeName != nullptr)
            {
                SigElementType returnElementType;
                std::vector<SigElementType> argElementTypes;
                if (SUCCEEDED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true)))
                {
                    const ULONG index = ((methodAttr & mdStatic) == 0) ? (i - 1) : i;
                    if (argElementTypes.size() > index)
                    {
                        *fallbackTypeName = argElementTypes.at(index).typeName;
                    }
                }
            }
            return Status;
        };

        IfFailRet(cb(to_utf8(wParamName.c_str()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
        usedNames.insert(wParamName);
        // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    for (uint32_t i = 0; i < cLocals; i++)
    {
        WSTRING wLocalName;
        if (FAILED(m_sharedDebugInfo->GetFrameNamedLocalVariable(trModule, methodDef, currentIlOffset, i, wLocalName)))
        {
            continue;
        }

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
        {
            if (trFrame == nullptr) // Forced to update trFrame/trILFrame.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
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
            IfFailRet(getValue(&trDisplayClassValue, nullptr, false));
            IfFailRet(WalkGeneratedClassFields(trMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                               m_sharedDebugInfo.get(), trModule, cb));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
            continue;
        }

        IfFailRet(cb(to_utf8(wLocalName.data()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
        usedNames.insert(wLocalName);
        // Reset trFrame/trILFrame, since it could be neutered at `cb` call, we need track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    // Enumerate local constants (literals) from PDB
    {
        std::vector<PDB::LocalConstant> localConstants;
        if (SUCCEEDED(m_sharedDebugInfo->GetLocalConstants(trModule, methodDef, currentIlOffset, localConstants)))
        {
            for (const auto &constant : localConstants)
            {
                if (usedNames.find(constant.name) != usedNames.end())
                {
                    continue;
                }

                // Skip compiler-generated constants
                if (IsSynthesizedLocalName(constant.name))
                {
                    continue;
                }

                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *, bool) -> HRESULT
                {
                    PCCOR_SIGNATURE pSig = constant.signature.data();
                    PCCOR_SIGNATURE pSigEnd = pSig + constant.signature.size();
                    return m_sharedEvalHelpers->CreateLiteralLocalValue(pThread, pSig, pSigEnd, ppResultValue);
                };

                IfFailRet(cb(to_utf8(constant.name.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_OK;
                }
                usedNames.insert(constant.name);
            }
        }
    }

    if (generatedCodeKind != GeneratedCodeKind::Normal)
    {
        IfFailRet(WalkGeneratedClassFields(trMDImport, trCurrentThis, currentIlOffset, usedNames, methodDef, m_sharedDebugInfo.get(), trModule, cb));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    if (trUserThis != nullptr && trUserThisClass != nullptr && TypeFromToken(userThisTypeDef) == mdtTypeDef)
    {
        IfFailRet(WalkPrimaryConstructorParameterFields(trMDImport, trUserThisClass, userThisTypeDef, trUserThis, usedNames, cb));
        // Note: WalkPrimaryConstructorParameterFields() could return S_CAN_EXIT.
    }
    return S_OK;
}

HRESULT Evaluator::FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                                ValueKind valueKind, const std::vector<std::string> &identifiers,
                                int nextIdentifier, ICorDebugValue **ppResult,
                                std::unique_ptr<Evaluator::SetterData> *resultSetterData)
{
    HRESULT Status = S_OK;

    // Note: in case of (nextIdentifier == identifiers.size()), the result is pValue itself, so we are OK here.
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    if (nextIdentifier > static_cast<int>(identifiers.size()))
    {
        return E_FAIL;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trResultValue(pValue);
    for (int i = nextIdentifier; i < static_cast<int>(identifiers.size()); i++)
    {
        if (identifiers.at(i).empty())
        {
            return E_FAIL;
        }

        const ToRelease<ICorDebugValue> trClassValue(trResultValue.Detach());

        IfFailRet(WalkMembers(trClassValue, pThread, frameLevel, (resultSetterData != nullptr),
            [&](ICorDebugType */*pType*/, bool isStatic, const std::string &memberName,
                const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData) -> HRESULT
            {
                if ((isStatic && valueKind == ValueKind::Variable) ||
                    (!isStatic && valueKind == ValueKind::Class) ||
                    memberName != identifiers.at(i))
                {
                    return S_OK;
                }

                IfFailRet(getValue(&trResultValue, nullptr, false));
                if (setterData != nullptr &&
                    resultSetterData != nullptr)
                {
                    *resultSetterData = std::make_unique<Evaluator::SetterData>(*setterData);
                }

                return S_CAN_EXIT; // Fast exit from loop.
            }));

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
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    const int identifiersNum = static_cast<int>(identifiers.size()) - 1;
    std::vector<std::string> fieldName{identifiers.back()};

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(EvalUtils::FindType(classIdentifiers, nextClassIdentifier, pThread, nullptr, nullptr, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        ToRelease<ICorDebugType> trType;
        nextClassIdentifier = 0;
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.begin(), identifiers.begin() + identifiersNum, std::back_inserter(fullpath));

        if (FAILED(EvalUtils::FindType(fullpath, nextClassIdentifier, pThread, trModule, &trType)))
        {
            break;
        }

        assert(fullpath.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if (nextClassIdentifier < static_cast<int>(fullpath.size()))
        {
            // try to check non-static fields inside a static member
            std::vector<std::string> staticName;
            for (int i = nextClassIdentifier; i < static_cast<int>(fullpath.size()); i++)
            {
                staticName.emplace_back(fullpath.at(i));
            }
            staticName.emplace_back(fieldName.at(0));
            ToRelease<ICorDebugValue> trTypeObject;
            // type has static members (S_NO_STATIC if type doesn't have static members)
            if (S_OK == m_sharedEvalHelpers->CreateTypeObjectStaticConstructor(pThread, trType, &trTypeObject))
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
        IfFailRet(m_sharedEvalHelpers->CreateTypeObjectStaticConstructor(pThread, trType, &trTypeObject));
        if (Status == S_OK && // type has static members (S_NO_STATIC if type doesn't have static members)
            SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Class, fieldName,
                                   0, ppResult, resultSetterData)))
        {
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT Evaluator::CallOverriddenToString(ICorDebugThread *pThread, ICorDebugValue *pInputValue, std::string &output)
{
    if ((GetEvalFlags() & EVAL_NOTOSTRING) != 0U)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    HRESULT Status = S_OK;

    ToRelease<ICorDebugValue2> trInputValue2;
    IfFailRet(pInputValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trInputValue2)));
    ToRelease<ICorDebugType> trInputType;
    IfFailRet(trInputValue2->GetExactType(&trInputType));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Evaluator::WalkMethods(trInputType, false, nullptr,
        [&](bool isStatic, const std::string &methodName, Evaluator::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (isStatic || !methodArgs.empty() || methodName != "ToString")
            {
                return S_OK; // Return with success to continue walk.
            }

            IfFailRet(getFunction(&trFunc));

            return S_CAN_EXIT; // Fast exit from loop, since we already found trFunc.
        }));

    if (trFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugValue> trRefValue;
    IfFailRet(m_sharedEvalHelpers->EvalFunction(pThread, trFunc, trInputType.GetPtr(), nullptr, &pInputValue, 1, &trRefValue, true));
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trRefValue, &trValue, nullptr));
    return PrintStringValue(trValue, output);
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
    else if (identifiers.at(nextIdentifier) == "$pid")
    {
        ToRelease<ICorDebugProcess> trProcess;
        IfFailRet(pThread->GetProcess(&trProcess));
        DWORD processId = 0;
        IfFailRet(trProcess->GetID(&processId));

        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_U4, nullptr, &trResolvedValue));

#ifdef DEBUG_INTERNAL_TESTS
        uint32_t cbSize = 0;
        IfFailRet(trResolvedValue->GetSize(&cbSize));
        assert(cbSize == 4);
#endif // DEBUG_INTERNAL_TESTS

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(trResolvedValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->SetValue(static_cast<void *>(&processId)));
    }
    else if (identifiers.at(nextIdentifier) == "$tid")
    {
        DWORD threadId = 0;
        IfFailRet(pThread->GetID(&threadId));

        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_U4, nullptr, &trResolvedValue));

#ifdef DEBUG_INTERNAL_TESTS
        uint32_t cbSize = 0;
        IfFailRet(trResolvedValue->GetSize(&cbSize));
        assert(cbSize == 4);
#endif // DEBUG_INTERNAL_TESTS

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(trResolvedValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->SetValue(static_cast<void *>(&threadId)));
    }
    else
    {
        IfFailRet(WalkStackVars(pThread, frameLevel,
            [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
            {
                if (name == "this")
                {
                    if (FAILED(getValue(&trThisValue, nullptr, false)) || (trThisValue == nullptr))
                    {
                        return S_OK;
                    }

                    if (name == identifiers.at(nextIdentifier))
                    {
                        return S_CAN_EXIT; // Fast way to exit from stack vars walk routine.
                    }
                }
                else if (name == identifiers.at(nextIdentifier))
                {
                    if (FAILED(getValue(&trResolvedValue, nullptr, false)) || (trResolvedValue == nullptr))
                    {
                        return S_OK;
                    }

                    return S_CAN_EXIT; // Fast way to exit from stack vars walk routine.
                }

                return S_OK;
            }));
    }

    if ((trResolvedValue == nullptr) && (trThisValue != nullptr)) // check this/this.*
    {
        if (identifiers.at(nextIdentifier) == "this")
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
        IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        std::string methodClass;
        std::string methodName;
        TypePrinter::GetTypeAndMethodName(trFrame, m_sharedDebugInfo.get(), methodClass, methodName);

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
        assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
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
        IfFailRet(EvalUtils::FindType(identifiers, nextIdentifier, pThread, nullptr, &trType));
        IfFailRet(m_sharedEvalHelpers->CreateTypeObjectStaticConstructor(pThread, trType, &trResolvedValue, false));

        // Identifiers resolved into type, not value. In case type could be result - provide type directly as result.
        // In this way caller will know, that no object instance here (should operate with static members/methods only).
        assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if ((ppResultType != nullptr) && nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        if (Status == S_NO_STATIC || // type don't have static members, nothing explore here
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

HRESULT Evaluator::LookupExtensionMethods(ICorDebugThread *pThread, ICorDebugType *pType, const std::string &methodName,
                                          std::vector<SigElementType> &methodArgs, std::vector<SigElementType> &/*methodGenerics*/,
                                          ICorDebugFunction **ppCorFunc)
{
    static constexpr std::string_view attributeName("System.Runtime.CompilerServices.ExtensionAttribute..ctor");
    HRESULT Status = S_OK;

    std::vector<SigElementType> typeGenerics;
    IfFailRet(GetTypeGenerics(pType, typeGenerics));

    IfFailRet(Modules::ForEachModule(pThread, [&](ICorDebugModule *pModule) -> HRESULT
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
                ULONG nameLen = 0;
                if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, nullptr, 0, &nameLen,
                                                      nullptr, nullptr, nullptr, nullptr, nullptr)))
                {
                    continue;
                }

                mdTypeDef memTypeDef = mdTypeDefNil;
                std::vector<WCHAR> szFuncName(nameLen, '\0');
                PCCOR_SIGNATURE pSig = nullptr;
                ULONG cbSig = 0;
                if (FAILED(trMDImport->GetMethodProps(mdMethod, &memTypeDef, szFuncName.data(), nameLen, nullptr,
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

                SigElementType returnElementType;
                std::vector<SigElementType> argElementTypes;
                if (FAILED(ParseMethodSig(trMDImport, mdMethod, pSig, pSig + cbSig, returnElementType, argElementTypes)))
                {
                    continue;
                }

                // TODO
                // argElementTypes - typeGenerics, methodGenerics

                typeName.clear();
                CorElementType ty = ELEMENT_TYPE_MAX;

                if (FAILED(pType->GetType(&ty)) ||
                    FAILED(TypePrinter::NameForTypeByType(pType, typeName)))
                {
                    continue;
                }
                if (ty == ELEMENT_TYPE_CLASS || ty == ELEMENT_TYPE_VALUETYPE)
                {
                    if (typeName != argElementTypes.at(0).typeName)
                    {
                        // If type names don't match, check implemented interfaces' names

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
                            pSig = nullptr;
                            cbSig = 0;
                            SigElementType ifaceElementType;
                            if (FAILED(trMDImportInt->GetInterfaceImplProps(ifaceImpl, &tkClass, &tkIface)))
                            {
                                continue;
                            }
                            if (TypeFromToken(tkIface) == mdtTypeSpec)
                            {
                                if (FAILED(trMDImportInt->GetTypeSpecFromToken(tkIface, &pSig, &cbSig)) ||
                                    FAILED(ParseElementType(trMDImportInt, pSig, pSig + cbSig, 0, ifaceElementType, false)))
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

                            if (ifaceElementType.typeName == argElementTypes.at(0).typeName &&
                                methodArgs.size() + 1 == argElementTypes.size())
                            {
                                bool found = true;
                                for (unsigned int i = 0; i < methodArgs.size(); i++)
                                {
                                    if (methodArgs.at(i).corType != argElementTypes.at(i + 1).corType)
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
                                    return S_CAN_EXIT; // Fast exit from loop.
                                }
                            }
                        }
                        trMDImportInt->CloseEnum(ifEnum);
                    }
                }
                else if (ty != argElementTypes.at(0).corType || (methodArgs.size() + 1 != argElementTypes.size()))
                {
                    continue;
                }
                else
                {
                    bool found = true;
                    for (unsigned int i = 0; i < methodArgs.size(); i++)
                    {
                        if (methodArgs.at(i).corType != argElementTypes.at(i + 1).corType)
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
                        return S_CAN_EXIT; // Fast exit from loop.
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

HRESULT Evaluator::ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

    m_debuggerTypeProxyCheckedTypes.erase(modAddress);
    m_debuggerTypeProxyCache.erase(modAddress);
    m_debuggerTypeProxyModuleCache.erase(modAddress);

    return S_OK;
}

} // namespace dncdbg
