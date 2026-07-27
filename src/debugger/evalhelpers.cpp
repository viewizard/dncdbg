// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalhelpers.h"
#include "debugger/evalstackmachine.h"
#include "debugger/evaluator.h"
#include "debugger/evalwaiter.h"
#include "debugger/valueprint.h"
#include "metadata/corhelpers.h"
#include "metadata/modules.h"
#include "utils/hresult.h"
#include "utils/utf.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace dncdbg
{

namespace
{

mdMethodDef GetMethodToken(IMetaDataImport *pMDImport, mdTypeDef typeDef, const WSTRING &methodName)
{
    ULONG numMethods = 0;
    HCORENUM mEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    pMDImport->EnumMethodsWithName(&mEnum, typeDef, methodName.c_str(), &methodDef, 1, &numMethods);
    pMDImport->CloseEnum(mEnum);
    return methodDef;
}

bool TypeHasStaticMembers(ICorDebugType *pType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pType->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(trMDImport->EnumFields(&hEnum, typeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        DWORD fieldAttr = 0;
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, nullptr, &fieldAttr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if ((fieldAttr & fdStatic) != 0U)
        {
            trMDImport->CloseEnum(hEnum);
            return true;
        }
    }
    trMDImport->CloseEnum(hEnum);

    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    while (SUCCEEDED(trMDImport->EnumProperties(&propEnum, typeDef, &propertyDef, 1, &numProperties)) && numProperties != 0)
    {
        mdMethodDef mdGetter = mdMethodDefNil;
        if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr,
                                                nullptr, nullptr, nullptr, nullptr, &mdGetter, nullptr, 0, nullptr)))
        {
            continue;
        }

        DWORD getterAttr = 0;
        if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                              nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if ((getterAttr & mdStatic) != 0U)
        {
            trMDImport->CloseEnum(propEnum);
            return true;
        }
    }
    trMDImport->CloseEnum(propEnum);

    return false;
}

} // unnamed namespace

// From strike.cpp
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull)
{
    *ppOutputValue = nullptr;
    if (pIsNull != nullptr)
    {
        *pIsNull = FALSE;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trCurrentValue(pValue);
    HRESULT Status = S_OK;

    while (true)
    {
        ToRelease<ICorDebugReferenceValue> trReferenceValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue))))
        {
            BOOL isNull = FALSE;
            IfFailRet(trReferenceValue->IsNull(&isNull));
            if (isNull == FALSE)
            {
                ToRelease<ICorDebugValue> trDereferencedValue;
                IfFailRet(trReferenceValue->Dereference(&trDereferencedValue));
                trCurrentValue = trDereferencedValue.Detach();
                continue;
            }
            else
            {
                if (pIsNull != nullptr)
                {
                    *pIsNull = TRUE;
                }
                break; // unboxed until null reference
            }
        }

        ToRelease<ICorDebugBoxValue> trBoxedValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugBoxValue, reinterpret_cast<void **>(&trBoxedValue))))
        {
            ToRelease<ICorDebugObjectValue> trUnboxedValue;
            IfFailRet(trBoxedValue->GetObject(&trUnboxedValue));
            trCurrentValue = trUnboxedValue.Detach();
            continue;
        }

        break; // unboxed until object
    }

    trCurrentValue->AddRef();
    *ppOutputValue = trCurrentValue;
    return S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    if (trType == nullptr)
    {
        return E_FAIL;
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

    ToRelease<ICorDebugObjectValue> trObjValue;
    ToRelease<ICorDebugValue> trUnboxedResultValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &trUnboxedResultValue));
    IfFailRet(trUnboxedResultValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(trMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        WSTRING mdName(nameLen, '\0');
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L24
        if (mdName == W("value"))
        {
            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppValueValue));
        }

        // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L23
        if (mdName == W("hasValue"))
        {
            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppHasValueValue));
        }
    }

    return (*ppValueValue == nullptr || *ppHasValueValue == nullptr) ? E_FAIL : S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, bool &hasValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trHasValueValue;
    IfFailRet(GetNullableValue(pValue, ppValueValue, &trHasValueValue));

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trHasValueValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    uint8_t boolHasValue = 0;
    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    if (cbSize != sizeof(boolHasValue))
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&boolHasValue));

    hasValue = (boolHasValue == 1);

    return S_OK;
}

void ParseFormatSpecifier(const std::string &expressionWithFormat, std::string &expression, FormatSpecifier &specifier)
{
    // Format specifiers
    // https://learn.microsoft.com/en-us/visualstudio/debugger/format-specifiers-in-csharp?view=visualstudio
    static const std::unordered_map<std::string_view, FormatSpecifier> formatMap{
        {"ac",      FormatSpecifier::ForceEvaluation},
        {"d",       FormatSpecifier::DecimalInteger},
        {"h",       FormatSpecifier::HexadecimalInteger},
        {"dynamic", FormatSpecifier::Dynamic},
        {"nse",     FormatSpecifier::EvaluatesWithNoSideEffects},
        {"nq",      FormatSpecifier::StringWithNoQuotes},
        {"hidden",  FormatSpecifier::DisplaysHiddenMembers},
        {"raw",     FormatSpecifier::DisplaysInRawMode},
        {"results", FormatSpecifier::Results}
    };

    specifier = FormatSpecifier::None;
    expression = expressionWithFormat;

    // Find the last comma to isolate the potential suffix
    size_t commaPos = expression.rfind(',');

    while (commaPos != std::string::npos)
    {
        // Extract the tail substring strictly after the comma
        const std::string_view tail = std::string_view(expression).substr(commaPos + 1);

        auto find = formatMap.find(tail);
        if (find == formatMap.end())
        {
            // Stop as soon as a comma-separated tail is not a known specifier:
            // the remaining text is the actual expression, which may legitimately
            // contain commas (e.g. multi-dimensional array access like arr[0,1]).
            break;
        }

        specifier = specifier | find->second;
        expression.resize(commaPos);

        commaPos = expression.rfind(',');
    }
}

HRESULT FindFunctionInModule(ICorDebugThread *pThread, const std::string &moduleFileName, const WSTRING &typeName,
                             const WSTRING &methodName, ICorDebugFunction **ppFunction)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(Modules::GetModuleWithName(pThread, moduleFileName, &trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trMDImport->FindTypeDefByName(typeName.c_str(), mdTypeDefNil, &typeDef));

    const mdMethodDef methodDef = GetMethodToken(trMDImport, typeDef, methodName);

    if (methodDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    return trModule->GetFunctionFromToken(methodDef, ppFunction);
}

void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts)
{
    size_t pos = 0;
    size_t prevPos = 0;

    while ((pos = textWithEval.find('{', prevPos)) != std::string::npos)
    {
        // Add text before the '{' (if any) as literal text.
        if (pos > prevPos)
        {
            textWithEvalParts.emplace_back(textWithEval.substr(prevPos, pos - prevPos), false);
        }

        // Find the matching closing '}' by counting brace depth.
        size_t endPos = pos + 1;
        int braceDepth = 1;
        while (endPos < textWithEval.length() && braceDepth > 0)
        {
            if (textWithEval.at(endPos) == '{')
            {
                braceDepth++;
            }
            else if (textWithEval.at(endPos) == '}')
            {
                braceDepth--;
            }
            endPos++;
        }

        if (braceDepth > 0)
        {
            // No matching closing brace found, treat from '{' to end as literal text.
            textWithEvalParts.emplace_back(textWithEval.substr(pos), false);
            prevPos = textWithEval.length();
            break;
        }

        // Add the expression inside braces (without the braces themselves) as expression.
        // endPos points to position after the matching '}', so expression is [pos+1, endPos-1).
        textWithEvalParts.emplace_back(textWithEval.substr(pos + 1, endPos - pos - 2), true);

        prevPos = endPos;
    }

    // Add remaining text after the last '}' (or entire string if no braces found) as literal text.
    if (prevPos < textWithEval.length())
    {
        textWithEvalParts.emplace_back(textWithEval.substr(prevPos), false);
    }
}

void BuildTextWithEval(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread, ICorDebugValue *pForcedThisValue,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output)
{
    // Build the final output text by evaluating expressions.
    for (const auto &[text, isExpression] : textWithEvalParts)
    {
        if (!isExpression)
        {
            // Literal text - append directly.
            output += text;
        }
        else
        {
            // Expression - evaluate it.
            FormatSpecifier specifier = FormatSpecifier::None;
            std::string expression;
            ParseFormatSpecifier(text, expression, specifier);

            std::string value;
            std::string errorText;
            ToRelease<ICorDebugValue> trResultValue;
            if (SUCCEEDED(pEvalStackMachine->EvaluateExpression(pThread, FrameLevel{0}, expression,
                                                                pForcedThisValue == nullptr ? specifier : specifier | FormatSpecifier::DisplaysInRawMode,
                                                                pForcedThisValue, &trResultValue, errorText)) &&
                SUCCEEDED(PrintValue(pThread, pEvaluator, pEvalStackMachine, trResultValue, specifier, value)))
            {
                output += value;
            }
            else
            {
                if (!errorText.empty())
                {
                    output += "{" + errorText + "}";
                }
                else
                {
                    output += "{unknown error}";
                }
            }
        }
    }
}

void EvalHelpers::Cleanup()
{
    m_trSuppressFinalizeMutex.lock();
    if (m_trSuppressFinalize != nullptr)
    {
        m_trSuppressFinalize.Free();
    }
    m_trSuppressFinalizeMutex.unlock();

    m_typeObjectCacheMutex.lock();
    m_typeObjectCache.clear();
    m_typeObjectCacheMutex.unlock();
}

HRESULT EvalHelpers::CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString)
{
    auto value16t = to_utf16(value);
    return m_sharedEvalWaiter->WaitEvalResult(pThread, ppNewString,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            HRESULT Status = S_OK;
            IfFailRet(pEval->NewString(value16t.c_str()));
            return S_OK;
        });
}

// Call managed function in debuggee process.
// [in] pThread - managed thread for evaluation;
// [in] pFunc - function to call;
// [in] pArgType - pointer to arg Type, could be nullptr;
// [in] ppArgsValue - pointer to args Value array, could be nullptr;
// [in] argsValueCount - size of args Value array;
// [out] ppEvalResult - return value;
HRESULT EvalHelpers::EvalFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType *pArgType,
                                  std::vector<ToRelease<ICorDebugType>> *pTrMethodGenericTypes,
                                  ICorDebugValue **ppArgsValue, uint32_t argsValueCount,
                                  FormatSpecifier specifier, ICorDebugValue **ppEvalResult)
{
    assert((!ppArgsValue && argsValueCount == 0) || (ppArgsValue && argsValueCount > 0));

    if ((specifier & FormatSpecifier::ForceEvaluation) == FormatSpecifier::None &&
        (GetEvalFlags() & EVAL_NOFUNCEVAL) != 0U)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    if (pArgType != nullptr)
    {
        ToRelease<ICorDebugTypeEnum> trTypeEnum;
        if (SUCCEEDED(pArgType->EnumerateTypeParameters(&trTypeEnum)))
        {
            ICorDebugType *pCurType = nullptr;
            ULONG fetched = 0;
            while (SUCCEEDED(trTypeEnum->Next(1, &pCurType, &fetched)) && fetched == 1)
            {
                trTypeParams.emplace_back(pCurType);
            }
        }
    }
    if (pTrMethodGenericTypes != nullptr)
    {
        trTypeParams.reserve(trTypeParams.size() + (*pTrMethodGenericTypes).size());
        std::transform((*pTrMethodGenericTypes).begin(), (*pTrMethodGenericTypes).end(),
                       std::back_inserter(trTypeParams), [](auto &entry)
                       {
                           return ToRelease<ICorDebugType>(entry.Detach());
                       });
        (*pTrMethodGenericTypes).clear();
    }

    return m_sharedEvalWaiter->WaitEvalResult(pThread, ppEvalResult,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            HRESULT Status = S_OK;
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
#ifdef BIT64
            assert(trTypeParams.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
            IfFailRet(trEval2->CallParameterizedFunction(pFunc, static_cast<uint32_t>(trTypeParams.size()),
                                                         reinterpret_cast<ICorDebugType **>(trTypeParams.data()),
                                                         argsValueCount, ppArgsValue));
            return S_OK;
        });
}

HRESULT EvalHelpers::TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult)
{
    const std::scoped_lock<std::mutex> lock(m_typeObjectCacheMutex);

    HRESULT Status = S_OK;
    ToRelease<ICorDebugType2> trType2;
    IfFailRet(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2)));

    COR_TYPEID typeID;
    IfFailRet(trType2->GetTypeID(&typeID));

    auto is_same = [&typeID](const type_object_t &typeObject)
                   {
                       return typeObject.m_TypeID.token1 == typeID.token1 && typeObject.m_TypeID.token2 == typeID.token2;
                   };
    auto it = std::find_if(m_typeObjectCache.begin(), m_typeObjectCache.end(), is_same);
    if (it == m_typeObjectCache.end())
    {
        return E_FAIL;
    }

    // Move data to the front, so the most recently used item is at the beginning.
    if (it != m_typeObjectCache.begin())
    {
        m_typeObjectCache.splice(m_typeObjectCache.begin(), m_typeObjectCache, it);
    }

    if (ppTypeObjectResult != nullptr)
    {
        // We don't check handle's status here, since we store only strong handles.
        // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/cordebughandletype-enumeration
        // The handle is strong, which prevents an object from being reclaimed by garbage collection.
        return m_typeObjectCache.front().m_trTypeObject->QueryInterface(IID_ICorDebugValue, reinterpret_cast<void **>(ppTypeObjectResult));
    }

    return S_OK;
}

HRESULT EvalHelpers::AddTypeObjectToCache(ICorDebugType *pType, ICorDebugValue *pTypeObject)
{
    const std::scoped_lock<std::mutex> lock(m_typeObjectCacheMutex);

    HRESULT Status = S_OK;
    ToRelease<ICorDebugType2> trType2;
    IfFailRet(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2)));

    COR_TYPEID typeID;
    IfFailRet(trType2->GetTypeID(&typeID));

    auto is_same = [&typeID](const type_object_t &typeObject)
                   {
                       return typeObject.m_TypeID.token1 == typeID.token1 && typeObject.m_TypeID.token2 == typeID.token2;
                   };
    auto it = std::find_if(m_typeObjectCache.begin(), m_typeObjectCache.end(), is_same);
    if (it != m_typeObjectCache.end())
    {
        return S_OK;
    }

    ToRelease<ICorDebugHandleValue> trHandleValue;
    IfFailRet(pTypeObject->QueryInterface(IID_ICorDebugHandleValue, reinterpret_cast<void **>(&trHandleValue)));

    CorDebugHandleType handleType = CorDebugHandleType::HANDLE_PINNED;
    if (FAILED(trHandleValue->GetHandleType(&handleType)) ||
        // Note, we need only strong or pinned handle here, that will not invalidated on continue-break.
        handleType == CorDebugHandleType::HANDLE_WEAK_TRACK_RESURRECTION)
    {
        return E_FAIL;
    }

    if (m_typeObjectCache.size() == m_typeObjectCacheSize)
    {
        // Re-use last list entry.
        m_typeObjectCache.back().m_TypeID = typeID;
        m_typeObjectCache.back().m_trTypeObject = trHandleValue.Detach();
        static_assert(m_typeObjectCacheSize >= 2);
        m_typeObjectCache.splice(m_typeObjectCache.begin(), m_typeObjectCache, std::prev(m_typeObjectCache.end()));
    }
    else
    {
        m_typeObjectCache.emplace_front(type_object_t{typeID, ToRelease<ICorDebugHandleValue>(trHandleValue.Detach())});
    }

    return S_OK;
}

HRESULT EvalHelpers::CreateTypeObjectStaticConstructor(ICorDebugThread *pThread, ICorDebugType *pType,
                                                       ICorDebugValue **ppTypeObjectResult, bool DetectStaticMembers)
{
    HRESULT Status = S_OK;

    CorElementType et = ELEMENT_TYPE_MAX;
    IfFailRet(pType->GetType(&et));

    if ((et != ELEMENT_TYPE_CLASS && et != ELEMENT_TYPE_VALUETYPE) ||
        SUCCEEDED(TryReuseTypeObjectFromCache(pType, ppTypeObjectResult))) // Check cache first, before check type for static members.
    {
        return S_OK;
    }

    // Create type object only in case type has static members.
    // Note: for some cases, static members are checked outside this method.
    if (DetectStaticMembers && !TypeHasStaticMembers(pType))
    {
        return S_NO_STATIC;
    }

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ICorDebugType *pCurType = nullptr;
        ULONG fetched = 0;
        while (SUCCEEDED(trTypeEnum->Next(1, &pCurType, &fetched)) && fetched == 1)
        {
            trTypeParams.emplace_back(pCurType);
        }
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pType->GetClass(&trClass));

    ToRelease<ICorDebugValue> trTypeObject;
    Status = m_sharedEvalWaiter->WaitEvalResult(pThread, &trTypeObject,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
#ifdef BIT64
            assert(trTypeParams.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
            IfFailRet(trEval2->NewParameterizedObjectNoConstructor(trClass, static_cast<uint32_t>(trTypeParams.size()),
                                                                   reinterpret_cast<ICorDebugType **>(trTypeParams.data())));
            return S_OK;
        });
    // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
    IfFailRet(Status);

    if (et == ELEMENT_TYPE_CLASS)
    {
        const std::scoped_lock<std::mutex> lock(m_trSuppressFinalizeMutex);

        if (m_trSuppressFinalize == nullptr)
        {
            static const std::string moduleFileName("System.Private.CoreLib.dll");
            static const WSTRING gcTypeName(W("System.GC"));
            static const WSTRING suppressFinalizeMethodName(W("SuppressFinalize"));
            IfFailRet(FindFunctionInModule(pThread, moduleFileName, gcTypeName, suppressFinalizeMethodName, &m_trSuppressFinalize));
        }

        if (m_trSuppressFinalize == nullptr)
        {
            return E_FAIL;
        }

        // Note: this call must ignore any eval flags.
        IfFailRet(EvalFunction(pThread, m_trSuppressFinalize, pType, nullptr, trTypeObject.GetRef(),
                               1, FormatSpecifier::ForceEvaluation, nullptr));
    }

    AddTypeObjectToCache(pType, trTypeObject);

    if (ppTypeObjectResult != nullptr)
    {
        *ppTypeObjectResult = trTypeObject.Detach();
    }

    return S_OK;
}

HRESULT EvalHelpers::CreateLiteralFieldValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                             UVCP_CONSTANT pRawValue, ULONG rawValueLength, ICorDebugValue **ppLiteralValue)
{
    // https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/constants
    // Only the C# built-in types may be declared as const. Reference type constants other than String can only be initialized
    // with a null value. User-defined types, including classes, structs, and arrays, cannot be const.

    // The signature format is: FIELD CustomMod* Type

    if (pRawValue == nullptr ||
        pThread == nullptr ||
        ppLiteralValue == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    // Skip calling convention with IMAGE_CEE_CS_CALLCONV_FIELD, since we are sure this is a field.
    IfFailRet(CorSigUncompressSkipOneByte_EndPtr(pSig, pSigEnd));

    // TODO care about "CustomMod*"

    CorElementType underlyingType = ELEMENT_TYPE_MAX;
    IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, underlyingType));

    if (underlyingType == ELEMENT_TYPE_STRING)
    {
        // https://learn.microsoft.com/en-us/dotnet/core/unmanaged-api/metadata/interfaces/imetadataimport-getfieldprops-method
        // pcchValue [out] The size in chars of ppValue, or zero if no string exists.
        // In case of ELEMENT_TYPE_STRING this is WCHAR, convert to length in bytes
        // since CreateLiteralValueImpl() counts on this.
        rawValueLength = rawValueLength * sizeof(WCHAR);
    }

    return CreateLiteralValueImpl(pThread, pSig, pSigEnd, underlyingType, pRawValue, rawValueLength, ppLiteralValue);
}

HRESULT EvalHelpers::CreateLiteralLocalValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                             ICorDebugValue **ppLiteralValue)
{
    // https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/constants
    // Only the C# built-in types may be declared as const. Reference type constants other than String can only be initialized
    // with a null value. User-defined types, including classes, structs, and arrays, cannot be const.

    // For local constants, the value is encoded in the signature
    // The signature format is: CustomMod* Type Value

    if (pThread == nullptr ||
        pSig == nullptr ||
        pSigEnd == nullptr ||
        ppLiteralValue == nullptr)
    {
        return E_INVALIDARG;
    }

    // TODO care about "CustomMod*"

    HRESULT Status = S_OK;
    CorElementType underlyingType = ELEMENT_TYPE_MAX;
    IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSigEnd, underlyingType));

    const UVCP_CONSTANT pRawValue = pSig;
    auto rawValueLength = static_cast<ULONG>(pSigEnd - pSig);

    static constexpr uint8_t nullStringMarker = 0xFF;
    if (underlyingType == ELEMENT_TYPE_STRING &&
        *reinterpret_cast<const uint8_t *>(pRawValue) == nullStringMarker)
    {
        rawValueLength = 0;
    }

    return CreateLiteralValueImpl(pThread, pSig, pSigEnd, underlyingType, pRawValue, rawValueLength, ppLiteralValue);
}

HRESULT EvalHelpers::CreateLiteralValueImpl(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                            CorElementType underlyingType, UVCP_CONSTANT pRawValue, ULONG rawValueLength,
                                            ICorDebugValue **ppLiteralValue)
{
    if (pThread == nullptr || pSig == nullptr || pSigEnd == nullptr || ppLiteralValue == nullptr)
    {
        return E_POINTER;
    }

    *ppLiteralValue = nullptr;
    HRESULT Status = S_OK;

    auto createTypeDef = [&](mdTypeDef typeDef, ICorDebugModule *pModule) -> HRESULT
    {
        if (pModule == nullptr)
        {
            return E_POINTER;
        }

        ToRelease<ICorDebugClass> trClass;
        IfFailRet(pModule->GetClassFromToken(typeDef, &trClass));
        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, trClass, ppLiteralValue));
        return S_OK;
    };

    auto createTypeRef = [&](const WSTRING &refFullName) -> HRESULT
    {
        if (refFullName.empty())
        {
            return E_INVALIDARG;
        }

        return Modules::ForEachModule(pThread,
            [&](ICorDebugModule *pModule) -> HRESULT
            {
                if (pModule == nullptr)
                {
                    return E_POINTER;
                }

                ToRelease<IUnknown> trUnknownDef;
                IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknownDef));
                ToRelease<IMetaDataImport> trMDImportDef;
                IfFailRet(trUnknownDef->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImportDef)));

                mdTypeDef typeDef = mdTypeDefNil;
                if (FAILED(trMDImportDef->FindTypeDefByName(refFullName.c_str(), mdTypeDefNil, &typeDef)))
                {
                    return S_OK; // Return with success to continue walk.
                }

                IfFailRet(createTypeDef(typeDef, pModule));
                return S_CAN_EXIT; // Fast exit from the loop.
            });
    };

    auto createNullObjectValue = [&]() -> HRESULT
    {
        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, nullptr, ppLiteralValue));
        return S_OK;
    };

    switch (underlyingType)
    {
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_ARRAY:
        case ELEMENT_TYPE_SZARRAY:
        {
            // FIXME for arrays create reference to proper type instead of object
            IfFailRet(createNullObjectValue());
            break;
        }
        case ELEMENT_TYPE_CLASS:
        {
            mdToken typeToken = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, typeToken));

            ToRelease<ICorDebugFrame> trFrame;
            IfFailRet(pThread->GetActiveFrame(&trFrame));
            ToRelease<ICorDebugFunction> trFunction;
            IfFailRet(trFrame->GetFunction(&trFunction));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trFunction->GetModule(&trModule));

            if (TypeFromToken(typeToken) == mdtTypeDef)
            {
                IfFailRet(createTypeDef(typeToken, trModule));
            }
            else if (TypeFromToken(typeToken) == mdtTypeRef)
            {
                ToRelease<IUnknown> trUnknown;
                IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
                ToRelease<IMetaDataImport> trMDImport;
                IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

                // Note, IMetaDataImport::GetTypeRefProps() returns a fully-qualified name.
                ULONG refNameSize = 0;
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, nullptr, 0, &refNameSize));
                WSTRING refFullName(refNameSize, '\0');
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, refFullName.data(), refNameSize, nullptr));
                IfFailRet(createTypeRef(refFullName));
            }
            else if (TypeFromToken(typeToken) == mdtTypeSpec)
            {
                // FIXME create reference to proper type instead of object
                IfFailRet(createNullObjectValue());
            }
            else
            {
                return E_INVALIDARG;
            }
            break;
        }
        case ELEMENT_TYPE_STRING:
        {
            if (rawValueLength == 0)
            {
                IfFailRet(createTypeRef(W("System.String")));
            }
            else
            {
                const auto *strValue = reinterpret_cast<const WCHAR *>(pRawValue);
                const ULONG strLen = rawValueLength / sizeof(WCHAR);
                IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppLiteralValue,
                    [&](ICorDebugEval *pEval) -> HRESULT
                    {
                        // Note, this code execution is protected by EvalWaiter mutex.
                        ToRelease<ICorDebugEval2> trEval2;
                        IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
                        IfFailRet(trEval2->NewStringWithLength(strValue, strLen));
                        return S_OK;
                    }));
            }
            break;
        }
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
        {
            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(trEval->CreateValue(underlyingType, nullptr, &trValue));
            ToRelease<ICorDebugGenericValue> trGenericValue;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
            IfFailRet(trGenericValue->SetValue(const_cast<void *>(pRawValue))); // NOLINT(cppcoreguidelines-pro-type-const-cast)
            *ppLiteralValue = trValue.Detach();
            break;
        }
        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

} // namespace dncdbg
