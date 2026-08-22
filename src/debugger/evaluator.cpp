// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluator.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/evaluation/evalexec.h" // NOLINT(misc-include-cleaner)
#include "debugger/evaluation/evalwaiter.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/frames.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
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
                    return S_CAN_EXIT; // Fast exit from the loop.
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
                        return S_CAN_EXIT; // Fast exit from the loop.
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

HRESULT FollowNestedFindType(ICorDebugThread *pThread, const std::string &displayTypeName,
                             const PDB::ImportsAndAliases &pdbImports,
                             std::vector<std::string> &identifiers, ICorDebugType **ppResultType)
{
    HRESULT Status = S_OK;

    std::vector<std::string> classIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(displayTypeName);

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(MetadataHelpers::FindTypeModule(classIdentifiers, pThread, pdbImports, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.begin(), identifiers.end(), std::back_inserter(fullpath));

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(MetadataHelpers::FindType(fullpath, nextClassIdentifier, pThread, trModule, pdbImports, &trType)))
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

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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
            return S_CAN_EXIT; // Fast exit from the loop.
        }
        usedNames.insert(wParameterName);

        return S_OK;
    });
}

} // unnamed namespace

Evaluator::Evaluator(std::shared_ptr<DebugInfo> &sharedDebugInfo,
                     std::shared_ptr<EvalExec> &sharedEvalExec)
    : m_sharedDebugInfo(sharedDebugInfo),
      m_sharedEvalExec(sharedEvalExec),
      m_sharedTypeProxy(std::make_shared<TypeProxy>(sharedEvalExec))
{
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT Evaluator::WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                            std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef, DebugInfo *pDebugInfo,
                                            ICorDebugModule *pModule, const Evaluator::WalkStackVarsCallback &cb)
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

            auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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
                IfFailRet(getValue(&trDisplayClassValue, nullptr));
                IfFailRet(WalkGeneratedClassFields(pMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                                   pDebugInfo, pModule, cb));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
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
                    return S_CAN_EXIT; // Fast exit from the loop.
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
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                usedNames.insert(mdName);
            }
            return S_OK; // Return with success to continue walk.
        });
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

    std::vector<SigElementType> genericTypeParameters;
    IfFailRet(MetadataHelpers::GetGenericTypeParameters(pInputType, genericTypeParameters));

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

            std::vector<WCHAR> szFunctionName(nameLen, '\0');
            DWORD methodAttr = 0;
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, szFunctionName.data(), nameLen, nullptr,
                                                  &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            uint32_t methodGenParamCount = 0;
            if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType,
                                      argElementTypes, false, &methodGenParamCount)))
            {
                continue;
            }

            if (FAILED(ApplyGenericTypeParameters(genericTypeParameters, returnElementType)))
            {
                continue;
            }

            bool applyFailed = false;
            for (auto &argType : argElementTypes)
            {
                if (FAILED(ApplyGenericTypeParameters(genericTypeParameters, argType)))
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

            IfFailRet(cb(isStatic, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, methodGenParamCount, getFunction));
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
    COR_TYPEID typeID{0, 0};
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
        IfFailRet(m_sharedEvalExec->CreateTypeObject(pThread, pType, nullptr));
    }

    IfFailRet(pType->GetStaticFieldValue(fieldDef, trFrame, ppResultValue));

    return S_OK;
}

HRESULT Evaluator::WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                               bool provideSetterData, FormatSpecifier specifier, const WalkMembersCallback &cb)
{
    // Same behavior as MS vsdbg and MSVS C# debugger have - don't show enumeration members.
    if (IsEnumeration(pInputValue))
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    bool showInRaw = (specifier & FormatSpecifier::DisplaysInRawMode) != FormatSpecifier::None ||
                     (GetEvalFlags() & EVAL_SHOWRAWVALUES) != 0U;
    bool showHidden = (specifier & FormatSpecifier::DisplaysHiddenMembers) != FormatSpecifier::None;

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

        CorElementType inputElemType = ELEMENT_TYPE_MAX;
        IfFailRet(pFrontValue->GetType(&inputElemType));
        if (inputElemType == ELEMENT_TYPE_PTR)
        {
            auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
            {
                trValue->AddRef();
                *ppResultValue = trValue;
                return S_OK;
            };

            IfFailRet(cb(nullptr, false, "", getValue, nullptr, nullptr));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT; // Fast exit from the loop.
            }
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
                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                {
                    IfFailRet(trArrayValue->GetElementAtPosition(i, ppResultValue));
                    return S_OK;
                };

                IfFailRet(cb(nullptr, false, "[" + IndicesToStr(ind, base) + "]", getValue, nullptr, nullptr));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
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
            std::string displayTypeName;
            MetadataHelpers::GetFQDisplayTypeName(trType, displayTypeName);
            if (displayTypeName == "decimal")
            {
                return S_OK;
            }

            if (displayTypeName.back() == '?') // System.Nullable<T>
            {
                ToRelease<ICorDebugValue> trValueValue;
                bool hasValue = false;
                IfFailRet(GetNullableValue(trValue, &trValueValue, hasValue));

                if (hasValue)
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

            CorElementType elemType = ELEMENT_TYPE_MAX;
            IfFailRet(trType->GetType(&elemType));
            if (elemType == ELEMENT_TYPE_STRING)
            {
                return S_OK;
            }

            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef currentTypeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&currentTypeDef));

            if (!showInRaw && isNull == FALSE && !isTypeProxyValue &&
                (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE))
            {
                ToRelease<ICorDebugValue> trTypeProxyValue;
                if (SUCCEEDED(m_sharedTypeProxy->GetDebuggerTypeProxyValue(pThread, trModule, pFrontValue, trType,
                                                                           currentTypeDef, &trTypeProxyValue)))
                {
                    trWalkQueue.emplace_front(trTypeProxyValue.Detach(), true);
                    return S_OK;
                }
            }

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            IfFailRet(ForEachFields(trMDImport, currentTypeDef,
                [&](mdFieldDef fieldDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, fieldDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return with success to continue walk.
                    }

                    ULONG nameLen = 0;
                    DWORD fieldAttr = 0;
                    IfFailRet(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, &fieldAttr,
                                                        nullptr, nullptr, nullptr, nullptr, nullptr));

                    if (isTypeProxyValue && !showHidden &&
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
                        if (!showHidden && IsSynthesizedLocalName(mdName))
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        const bool isStatic = (fieldAttr & fdStatic);
                        if (isNull == TRUE && !isStatic)
                        {
                            return S_OK; // Return with success to continue walk.
                        }

                        const std::string name = to_utf8(mdName.c_str());

                        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *fallbackTypeName) -> HRESULT
                        {
                            if (fieldAttr & fdLiteral)
                            {
                                std::string realDisplayTypeName;
                                IfFailRet(m_sharedEvalExec->CreateLiteralFieldValue(pThread, pSig, pSig + cbSig, pRawValue,
                                                                                    rawValueLength, ppResultValue, realDisplayTypeName));

                                if (fallbackTypeName != nullptr)
                                {
                                    *fallbackTypeName = std::move(realDisplayTypeName);
                                }
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
                            if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                            {
                                trWalkQueue.emplace_back(trResultValue.Detach(), false);
                            }
                            return S_OK; // Return with success to continue walk.
                        }

                        std::string textWithEval;
                        HasDebuggerAttribute(trMDImport, fieldDef, DebuggerAttribute::Display, textWithEval);

                        IfFailRet(cb(trType, isStatic, name, getValue, nullptr, &textWithEval));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from the loop.
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
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, propertyDef);
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

                        if (isTypeProxyValue && !showHidden &&
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

                        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                        {
                            if (pThread == nullptr)
                            {
                                return E_FAIL;
                            }

                            ToRelease<ICorDebugFunction> trFunc;
                            IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                            return m_sharedEvalExec->CallFunction(pThread, trFunc, trType.GetPtr(), nullptr,
                                                                  isStatic ? nullptr : &pFrontValue, isStatic ? 0 : 1,
                                                                  specifier, ppResultValue);
                        };

                        if (browsableState == DebuggerBrowsableState::RootHidden)
                        {
                            ToRelease<ICorDebugValue> trResultValue;
                            if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                            {
                                trWalkQueue.emplace_back(trResultValue.Detach(), false);
                            }
                            return S_OK; // Return with success to continue walk.
                        }

                        std::string textWithEval;
                        HasDebuggerAttribute(trMDImport, propertyDef, DebuggerAttribute::Display, textWithEval);

                        if (provideSetterData)
                        {
                            ToRelease<ICorDebugFunction> trFuncSetter;
                            if (FAILED(trModule->GetFunctionFromToken(mdSetter, &trFuncSetter)))
                            {
                                trFuncSetter.Free();
                            }
                            Evaluator::SetterData setterData(isStatic ? nullptr : pFrontValue, trType, trFuncSetter);
                            IfFailRet(cb(trType, isStatic, name, getValue, &setterData, &textWithEval));
                            if (Status == S_CAN_EXIT)
                            {
                                return S_CAN_EXIT; // Fast exit from the loop.
                            }
                        }
                        else
                        {
                            IfFailRet(cb(trType, isStatic, name, getValue, nullptr, &textWithEval));
                            if (Status == S_CAN_EXIT)
                            {
                                return S_CAN_EXIT; // Fast exit from the loop.
                            }
                        }
                    }
                    return S_OK; // Return with success to continue walk.
                });
            // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
            IfFailRet(Status);
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }

            std::string displayBaseTypeName;
            ToRelease<ICorDebugType> trBaseType;
            if (SUCCEEDED(trType->GetBase(&trBaseType)) && trBaseType != nullptr &&
                SUCCEEDED(MetadataHelpers::GetFQDisplayTypeName(trBaseType, displayBaseTypeName)))
            {
                trType.Free();

                if (displayBaseTypeName != "object" && displayBaseTypeName != "System.Object" && displayBaseTypeName != "System.ValueType")
                {
                    if (pThread != nullptr)
                    {
                        m_sharedEvalExec->CreateTypeObject(pThread, trBaseType, nullptr);
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
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    return S_OK;
}

HRESULT Evaluator::GetFQDisplayTypeName(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &displayTypeName, bool &haveThis)
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

    std::list<std::string> args;
    MetadataHelpers::GetGenericArgs(trFrame, args);

    haveThis = ((methodAttr & mdStatic) == 0);
    // In case this is static method, this is not async/lambda case for sure.
    if (!haveThis)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
    }

    GeneratedCodeKind generatedCodeKind = GeneratedCodeKind::Normal;
    IfFailRet(GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
    if (generatedCodeKind == GeneratedCodeKind::Normal)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
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

    return MetadataHelpers::GetFQDisplayNameForToken(userTypeDef, trMDImport, displayTypeName, &args);
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
            auto getValue = [&](ICorDebugValue **, std::string *fallbackTypeName) -> HRESULT
            {
                if (fallbackTypeName != nullptr)
                {
                    MetadataHelpers::GetFQDisplayRealCodeTypeName(trFrame, m_sharedDebugInfo.get(), *fallbackTypeName);
                }
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
                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *fallbackTypeName) -> HRESULT
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
                        *fallbackTypeName = argElementTypes.at(index).metadataTypeName;
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

        auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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
            IfFailRet(getValue(&trDisplayClassValue, nullptr));
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

                auto getValue = [&](ICorDebugValue **ppResultValue, std::string *fallbackTypeName) -> HRESULT
                {
                    PCCOR_SIGNATURE pSig = constant.signature.data();
                    PCCOR_SIGNATURE pSigEnd = pSig + constant.signature.size();
                    std::string realDisplayTypeName;
                    IfFailRet(m_sharedEvalExec->CreateLiteralLocalValue(pThread, pSig, pSigEnd, ppResultValue, realDisplayTypeName));

                    if (fallbackTypeName != nullptr)
                    {
                        *fallbackTypeName = std::move(realDisplayTypeName);
                    }

                    return S_OK;
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
                                ValueKind valueKind, const std::vector<std::string> &identifiers, int nextIdentifier,
                                FormatSpecifier specifier, ICorDebugValue **ppResult, std::string *realDisplayTypeName,
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

        IfFailRet(WalkMembers(trClassValue, pThread, frameLevel, (resultSetterData != nullptr), specifier,
            [&](ICorDebugType */*pType*/, bool isStatic, const std::string &memberName,
                const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData, std::string *) -> HRESULT
            {
                if ((isStatic && valueKind == ValueKind::Variable) ||
                    (!isStatic && valueKind == ValueKind::Static) ||
                    memberName != identifiers.at(i))
                {
                    return S_OK;
                }

                if (FAILED(Status = getValue(&trResultValue, realDisplayTypeName)))
                {
                    if (realDisplayTypeName != nullptr)
                    {
                        (*realDisplayTypeName).clear();
                    }
                    return Status;
                }
                if (setterData != nullptr &&
                    resultSetterData != nullptr)
                {
                    *resultSetterData = std::make_unique<Evaluator::SetterData>(*setterData);
                }

                return S_CAN_EXIT; // Fast exit from the loop.
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

HRESULT Evaluator::FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &displayTypeName,
                                         std::vector<std::string> &identifiers, FormatSpecifier specifier,
                                         const PDB::ImportsAndAliases &pdbImports, ICorDebugValue **ppResult,
                                         std::string *realDisplayTypeName, std::unique_ptr<Evaluator::SetterData> *resultSetterData)
{
    HRESULT Status = S_OK;

    std::vector<std::string> classIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(displayTypeName);
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    const int identifiersNum = static_cast<int>(identifiers.size()) - 1;
    std::vector<std::string> fieldName{identifiers.back()};

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(MetadataHelpers::FindTypeModule(classIdentifiers, pThread, pdbImports, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.begin(), identifiers.begin() + identifiersNum, std::back_inserter(fullpath));

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(MetadataHelpers::FindType(fullpath, nextClassIdentifier, pThread, trModule, pdbImports, &trType)))
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
            if (TypeHasStaticMembers(trType) &&
                SUCCEEDED(m_sharedEvalExec->CreateTypeObject(pThread, trType, &trTypeObject)) &&
                SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Static, staticName,
                                       0, specifier, ppResult, realDisplayTypeName, resultSetterData)))
            {
                return S_OK;
            }
            trim = true;
            continue;
        }

        ToRelease<ICorDebugValue> trTypeObject;
        if (TypeHasStaticMembers(trType) &&
            SUCCEEDED(m_sharedEvalExec->CreateTypeObject(pThread, trType, &trTypeObject)) &&
            SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Static, fieldName,
                                   0, specifier, ppResult, realDisplayTypeName, resultSetterData)))
        {
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT Evaluator::CallOverriddenToString(ICorDebugThread *pThread, ICorDebugValue *pInputValue, FormatSpecifier specifier, std::string &output)
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
            std::vector<SigElementType> &methodArgs, uint32_t /*methodGenParamCount*/,
            const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
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
    IfFailRet(m_sharedEvalExec->CallFunction(pThread, trFunc, trInputType.GetPtr(), nullptr, &pInputValue,
                                             1, specifier, &trRefValue));
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trRefValue, &trValue, nullptr));
    return PrintStringValue(trValue, output);
}

HRESULT Evaluator::ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pForcedThisValue,
                                      SetterData *inputSetterData, std::vector<std::string> &identifiers,
                                      FormatSpecifier specifier, ICorDebugValue **ppResultValue, std::string *realDisplayTypeName,
                                      std::unique_ptr<SetterData> *resultSetterData, ICorDebugType **ppResultType)
{
    if (pForcedThisValue != nullptr && identifiers.empty())
    {
        pForcedThisValue->AddRef();
        *ppResultValue = pForcedThisValue;
        if (inputSetterData != nullptr && resultSetterData != nullptr)
        {
            *resultSetterData = std::make_unique<Evaluator::SetterData>(*inputSetterData);
        }
        return S_OK;
    }
    else if (pForcedThisValue != nullptr)
    {
        return FollowFields(pThread, frameLevel, pForcedThisValue, ValueKind::Variable, identifiers,
                            0, specifier, ppResultValue, realDisplayTypeName, resultSetterData);
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
                    if (FAILED(getValue(&trThisValue, realDisplayTypeName)) || (trThisValue == nullptr))
                    {
                        if (realDisplayTypeName != nullptr)
                        {
                            (*realDisplayTypeName).clear();
                        }
                        return S_OK;
                    }

                    if (name == identifiers.at(nextIdentifier))
                    {
                        return S_CAN_EXIT; // Fast way to exit from stack vars walk routine.
                    }
                }
                else if (name == identifiers.at(nextIdentifier))
                {
                    if (FAILED(getValue(&trResolvedValue, realDisplayTypeName)) || (trResolvedValue == nullptr))
                    {
                        if (realDisplayTypeName != nullptr)
                        {
                            (*realDisplayTypeName).clear();
                        }
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
                                   nextIdentifier, specifier, &trResolvedValue, realDisplayTypeName, resultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }
    }

    PDB::ImportsAndAliases pdbImports;
    GetImportsAndAliases(pThread, frameLevel, pdbImports);

    if (trResolvedValue == nullptr) // check statics in nested classes
    {
        ToRelease<ICorDebugFrame> trFrame;
        IfFailRet(GetFrameAt(pThread, frameLevel, m_sharedDebugInfo.get(), IsJustMyCode(), &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        std::string displayTypeName;
        MetadataHelpers::GetFQDisplayRealCodeTypeName(trFrame, m_sharedDebugInfo.get(), displayTypeName);

        if (SUCCEEDED(FollowNestedFindValue(pThread, frameLevel, displayTypeName, identifiers, specifier,
                                            pdbImports, &trResolvedValue, realDisplayTypeName, resultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }

        if (ppResultType != nullptr &&
            SUCCEEDED(FollowNestedFindType(pThread, displayTypeName, pdbImports, identifiers, ppResultType)))
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
        IfFailRet(MetadataHelpers::FindType(identifiers, nextIdentifier, pThread, nullptr, pdbImports, &trType));

        // Identifiers resolved into type, not value. In case type could be result - provide type directly as result.
        // In this way caller will know, that no object instance here (should operate with static members/methods only).
        assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if ((ppResultType != nullptr) && nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        if (nextIdentifier == static_cast<int>(identifiers.size()) || // no more identifiers to resolve into members
            !TypeHasStaticMembers(trType) || // type doesn't have static members, nothing to explore here
            FAILED(m_sharedEvalExec->CreateTypeObject(pThread, trType, &trResolvedValue)))
        {
            return E_INVALIDARG;
        }

        valueKind = ValueKind::Static;
    }

    ToRelease<ICorDebugValue> trResultValue;
    IfFailRet(FollowFields(pThread, frameLevel, trResolvedValue, valueKind, identifiers,
                           nextIdentifier, specifier, &trResultValue, realDisplayTypeName, resultSetterData));

    *ppResultValue = trResultValue.Detach();
    return S_OK;
}

HRESULT Evaluator::WalkExtensionMethods(ICorDebugType *pInputType, CorElementType elemType, const Evaluator::WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;

    std::unordered_set<std::string> allIfaceTypeNames;
    auto fillIfaceTypeNames = [&]() -> HRESULT
    {
        // Walk the type and all its base types, collecting the type name and all
        // implemented interfaces (including those inherited from base types). This is
        // required for extension method resolution on types whose interfaces are
        // declared on a base class (e.g. CastICollectionIterator<int>, whose
        // IEnumerable<TResult> is implemented by the base Iterator<TResult>).
        ToRelease<ICorDebugType> trCurrentType(pInputType);
        trCurrentType->AddRef();
        while (trCurrentType != nullptr)
        {
            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trCurrentType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef typeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&typeDef));
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
            std::string typeName;
            IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(typeDef, trMDImport, typeName));

            allIfaceTypeNames.emplace(typeName);

            HCORENUM hEnum = nullptr;
            mdInterfaceImpl ifaceImpl = mdInterfaceImplNil;
            ULONG pcImpls = 0;
            while (SUCCEEDED(trMDImport->EnumInterfaceImpls(&hEnum, typeDef, &ifaceImpl, 1, &pcImpls)) &&
                   pcImpls != 0)
            {
                mdToken tkIface = mdTokenNil;
                if (FAILED(trMDImport->GetInterfaceImplProps(ifaceImpl, nullptr, &tkIface)))
                {
                    continue;
                }

                std::string ifaceTypeName;
                if (FAILED(MetadataHelpers::GetFQMDTypeNameByToken(tkIface, trMDImport, ifaceTypeName)))
                {
                    continue;
                }

                allIfaceTypeNames.emplace(ifaceTypeName);
            }
            trMDImport->CloseEnum(hEnum);

            ToRelease<ICorDebugType> trBaseType;
            if (FAILED(trCurrentType->GetBase(&trBaseType)) || trBaseType == nullptr)
            {
                break;
            }
            trCurrentType = trBaseType.Detach();
        }
        return S_OK;
    };

    if (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE)
    {
        IfFailRet(fillIfaceTypeNames());
    }
    else if (elemType == ELEMENT_TYPE_SZARRAY)
    {
        // Note: arrays use metadata type name check, not elemType.
        std::string typeName;
        IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorType(pInputType, typeName));
        allIfaceTypeNames.emplace(typeName);

        // Base Class Library collection interfaces for arrays.
        for (const char *ifaceName : {
                    "System.Collections.Generic.IList`1",
                    "System.Collections.Generic.ICollection`1",
                    "System.Collections.Generic.IEnumerable`1",
                    "System.Collections.Generic.IReadOnlyList`1",
                    "System.Collections.Generic.IReadOnlyCollection`1",
                    "System.Collections.IList",
                    "System.Collections.ICollection",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }
    else if (elemType == ELEMENT_TYPE_ARRAY)
    {
        // Note: arrays use metadata type name check, not elemType.
        std::string typeName;
        IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorType(pInputType, typeName));
        allIfaceTypeNames.emplace(typeName);

        // Base Class Library collection interfaces for arrays.
        for (const char *ifaceName : {
                    "System.Collections.IList",
                    "System.Collections.ICollection",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }
    else if (elemType == ELEMENT_TYPE_STRING)
    {
        // Note: strings don't need a metadata type name, since they use elemType for the check.

        // Base Class Library collection interfaces for strings.
        for (const char *ifaceName : {
                    "System.Collections.Generic.IEnumerable`1",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }

    const std::scoped_lock<std::mutex> lock(m_extensionMethodsMutex);

    for (const auto &[modAddress, extensionMethods] : m_extensionMethodsCache)
    {
        ICorDebugModule *pModule = extensionMethods.trModule.GetPtr();

        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        for (const auto &methodDef : extensionMethods.methodDefs)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> szFunctionName(nameLen, '\0');
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, szFunctionName.data(), nameLen, nullptr,
                                                  nullptr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            uint32_t methodGenParamCount = 0;
            if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType,
                                      argElementTypes, false, &methodGenParamCount)))
            {
                continue;
            }

            if (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE ||
                elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
            {
                if (allIfaceTypeNames.find(argElementTypes.at(0).metadataTypeName) == allIfaceTypeNames.end())
                {
                    continue; // Type name didn't match, try next method
                }
            }
            else if (elemType == ELEMENT_TYPE_STRING)
            {
                if (elemType != argElementTypes.at(0).elemType &&
                    allIfaceTypeNames.find(argElementTypes.at(0).metadataTypeName) == allIfaceTypeNames.end())
                {
                    continue; // Type name didn't match, try next method
                }
            }
            else if (elemType != argElementTypes.at(0).elemType)
            {
                continue;
            }

            auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
            {
                return pModule->GetFunctionFromToken(methodDef, ppResultFunction);
            };

            // Pass `false` as isStatic - extension methods require `this` as their first parameter.
            // Note: extension methods explicitly provide `this` as first argument in argElementTypes.
            IfFailRet(cb(false, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, methodGenParamCount, getFunction));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
        }
    }

    return S_OK;
}

HRESULT Evaluator::FillModuleExtensionMethodsCache(ICorDebugModule *pModule)
{
    // https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.extensionattribute
    // Indicates that a method is an extension method, or that a class or assembly contains extension methods.
    static const WSTRING extensionAttribute(W("System.Runtime.CompilerServices.ExtensionAttribute"));
    HRESULT Status = S_OK;

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ToRelease<IMetaDataAssemblyImport> trAssemblyImport;
    mdAssembly assemblyToken = mdAssemblyNil;
    if (SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&trAssemblyImport))) &&
        SUCCEEDED(trAssemblyImport->GetAssemblyFromScope(&assemblyToken)) &&
        !HasAttribute(trMDImport, assemblyToken, extensionAttribute))
    {
        return S_OK;
    }

    std::vector<mdMethodDef> moduleMethodDefs;
    HCORENUM hTypeEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    ULONG fetchedTypes = 0;
    while (SUCCEEDED(trMDImport->EnumTypeDefs(&hTypeEnum, &typeDef, 1, &fetchedTypes)) && fetchedTypes != 0)
    {
        if (!HasAttribute(trMDImport, typeDef, extensionAttribute))
        {
            continue;
        }

        HCORENUM hMethodEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        ULONG fetchedMethods = 0;
        while (SUCCEEDED(trMDImport->EnumMethods(&hMethodEnum, typeDef, &methodDef, 1, &fetchedMethods)) && fetchedMethods != 0)
        {
            DWORD methodAttr = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                                  &methodAttr, nullptr, nullptr, nullptr, nullptr)) ||
                (methodAttr & (mdMemberAccessMask | mdStatic)) != (mdPublic | mdStatic) ||
                !HasAttribute(trMDImport, methodDef, extensionAttribute))
            {
                continue;
            }

            moduleMethodDefs.emplace_back(methodDef);
        }
        trMDImport->CloseEnum(hMethodEnum);
    }
    trMDImport->CloseEnum(hTypeEnum);

    if (!moduleMethodDefs.empty())
    {
        moduleMethodDefs.shrink_to_fit();

        const std::scoped_lock<std::mutex> lock(m_extensionMethodsMutex);

        pModule->AddRef();
        m_extensionMethodsCache.emplace(modAddress, ModuleExtensionMethods(pModule, std::move(moduleMethodDefs)));
    }

    return S_OK;
}

HRESULT Evaluator::ManagedCallbackLoadModule(ICorDebugModule *pModule, bool privateCoreLib)
{
    HRESULT Status = S_OK;
    IfFailRet(FillModuleExtensionMethodsCache(pModule));

    if (privateCoreLib)
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
        static const WSTRING strTypeDef(W("System.Enum"));
        IfFailRet(trMDImport->FindTypeDefByName(strTypeDef.c_str(), mdTypeDefNil, &m_systemEnumTypeDef));
        IfFailRet(pModule->GetBaseAddress(&m_systemEnumModAddress));
    }

    return S_OK;
}

HRESULT Evaluator::ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    m_sharedTypeProxy->ManagedCallbackUnloadModule(pModule);

    {
        const std::scoped_lock<std::mutex> lock(m_extensionMethodsMutex);

        m_extensionMethodsCache.erase(modAddress);
    }

    return S_OK;
}

void Evaluator::GetImportsAndAliases(ICorDebugThread *pThread, FrameLevel frameLevel, PDB::ImportsAndAliases &pdbImports)
{
    auto getImportsAndAliases = [&]() -> HRESULT
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

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        IfFailRet(m_sharedDebugInfo->GetImportsAndAliases(trModule, methodDef, currentIlOffset, pdbImports));

        auto applyTokenName = [&trMDImport](std::vector<PDB::Imports> &alias) -> HRESULT
        {
            for (auto &entry : alias)
            {
                // For TypeSpec tokens, pre-resolve the generic type arguments from the signature
                // so that GetFQDisplayNameForToken() can substitute them into the display name.
                std::list<std::string> args;
                std::list<std::string> *pArgs = nullptr;
                if (TypeFromToken(entry.token) == mdtTypeSpec)
                {
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    SigElementType sigType;
                    if (FAILED(trMDImport->GetTypeSpecFromToken(entry.token, &pSig, &cbSig)) ||
                        FAILED(ParseElementType(trMDImport, pSig, pSig + cbSig, 0, sigType, &args, true)))
                    {
                        // Skip entries whose TypeSpec signature cannot be parsed.
                        continue;
                    }
                    pArgs = &args;
                }

                if (FAILED(MetadataHelpers::GetFQDisplayNameForToken(entry.token, trMDImport, entry.displayName, pArgs)))
                {
                    // Skip entries whose target type cannot be resolved.
                    continue;
                }
            }

            return S_OK;
        };

        auto importType = pdbImports.find(PDB::ImportsKind::ImportType);
        if (importType != pdbImports.end())
        {
            applyTokenName(importType->second);
        }

        auto aliasType = pdbImports.find(PDB::ImportsKind::AliasType);
        if (aliasType != pdbImports.end())
        {
            applyTokenName(aliasType->second);
        }

        return S_OK;
    };

    pdbImports.clear();
    getImportsAndAliases();

    // In case of failure (or no debug info for this code), add the default "System" namespace.
    auto &importNamespace = pdbImports[PDB::ImportsKind::ImportNamespace];
    if (importNamespace.empty())
    {
        importNamespace.emplace_back();
        importNamespace.back().targetNamespace = "System";
    }
}

bool Evaluator::IsEnumeration(ICorDebugValue *pInputValue) const
{
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    if (FAILED(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull)) ||
        isNull == TRUE)
    {
        return false;
    }

    ToRelease<ICorDebugValue2> trValue2;
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugType> trBaseType;
    ToRelease<ICorDebugClass> trBaseClass;
    ToRelease<ICorDebugModule> trModule;
    CORDB_ADDRESS modAddress = 0;
    mdTypeDef typeDef = mdTypeDefNil;
    return SUCCEEDED(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) &&
           SUCCEEDED(trValue2->GetExactType(&trType)) &&
           SUCCEEDED(trType->GetBase(&trBaseType)) &&
           trBaseType != nullptr &&
           SUCCEEDED(trBaseType->GetClass(&trBaseClass)) &&
           SUCCEEDED(trBaseClass->GetModule(&trModule)) &&
           SUCCEEDED(trModule->GetBaseAddress(&modAddress)) &&
           modAddress == m_systemEnumModAddress &&
           SUCCEEDED(trBaseClass->GetToken(&typeDef)) &&
           typeDef == m_systemEnumTypeDef;
}

} // namespace dncdbg
