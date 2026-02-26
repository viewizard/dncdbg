// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalhelpers.h"
#include "debugger/evalutils.h"
#include "debugger/evalwaiter.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h" // NOLINT(misc-include-cleaner)
#include "metadata/sigparse.h"
#include "utils/platform.h"
#include "utils/utf.h"
#include <algorithm>
#include <vector>

namespace dncdbg
{

namespace
{

mdMethodDef GetMethodToken(IMetaDataImport *pMDImport, mdTypeDef cl, const WSTRING &methodName)
{
    ULONG numMethods = 0;
    HCORENUM mEnum = nullptr;
    mdMethodDef methodDef = mdTypeDefNil;
    pMDImport->EnumMethodsWithName(&mEnum, cl, methodName.c_str(), &methodDef, 1, &numMethods);
    pMDImport->CloseEnum(mEnum);
    return methodDef;
}

HRESULT FindFunction(ICorDebugModule *pModule, const WSTRING &typeName, const WSTRING &methodName, ICorDebugFunction **ppFunction)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;

    IfFailRet(trMDImport->FindTypeDefByName(typeName.c_str(), mdTypeDefNil, &typeDef));

    const mdMethodDef methodDef = GetMethodToken(trMDImport, typeDef, methodName);

    if (methodDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    return pModule->GetFunctionFromToken(methodDef, ppFunction);
}

bool TypeHaveStaticMembers(ICorDebugType *pType)
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
            // Note, this code execution protected by EvalWaiter mutex.
            HRESULT Status = S_OK;
            IfFailRet(pEval->NewString(value16t.c_str()));
            return S_OK;
        });
}

// Call managed function in debuggee process.
// [in] pThread - managed thread for evaluation;
// [in] pFunc - function to call;
// [in] ppArgsType - pointer to args Type array, could be nullptr;
// [in] ArgsTypeCount - size of args Type array;
// [in] ppArgsValue - pointer to args Value array, could be nullptr;
// [in] ArgsValueCount - size of args Value array;
// [out] ppEvalResult - return value;
HRESULT EvalHelpers::EvalFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType **ppArgsType,
                                  uint32_t ArgsTypeCount, ICorDebugValue **ppArgsValue, uint32_t ArgsValueCount,
                                  ICorDebugValue **ppEvalResult, bool ignoreEvalFlags)
{
    assert((!ppArgsType && ArgsTypeCount == 0) || (ppArgsType && ArgsTypeCount > 0));
    assert((!ppArgsValue && ArgsValueCount == 0) || (ppArgsValue && ArgsValueCount > 0));

    const uint32_t evalFlags = ignoreEvalFlags ? defaultEvalFlags : m_evalFlags;

    if ((evalFlags & EVAL_NOFUNCEVAL) != 0U)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    // Reserve memory from the beginning, since trTypeParams will have ArgsTypeCount or more count of elements for sure.
    trTypeParams.reserve(ArgsTypeCount);

    for (uint32_t i = 0; i < ArgsTypeCount; i++)
    {
        ToRelease<ICorDebugTypeEnum> trTypeEnum;
        if (SUCCEEDED(ppArgsType[i]->EnumerateTypeParameters(&trTypeEnum)))
        {
            ICorDebugType *curType = nullptr; // NOLINT(misc-const-correctness)
            ULONG fetched = 0;
            while (SUCCEEDED(trTypeEnum->Next(1, &curType, &fetched)) && fetched == 1)
            {
                trTypeParams.emplace_back(curType);
            }
        }
    }

    return m_sharedEvalWaiter->WaitEvalResult(pThread, ppEvalResult,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution protected by EvalWaiter mutex.
            HRESULT Status = S_OK;
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->CallParameterizedFunction(pFunc, static_cast<uint32_t>(trTypeParams.size()),
                                                         reinterpret_cast<ICorDebugType **>(trTypeParams.data()), ArgsValueCount, ppArgsValue));
            return S_OK;
        });
}

HRESULT EvalHelpers::EvalGenericFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType **ppArgsType,
                                         uint32_t ArgsTypeCount, ICorDebugValue **ppArgsValue, uint32_t ArgsValueCount,
                                         ICorDebugValue **ppEvalResult)
{
    assert((!ppArgsType && ArgsTypeCount == 0) || (ppArgsType && ArgsTypeCount > 0));
    assert((!ppArgsValue && ArgsValueCount == 0) || (ppArgsValue && ArgsValueCount > 0));

    if ((m_evalFlags & EVAL_NOFUNCEVAL) != 0U)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    return m_sharedEvalWaiter->WaitEvalResult(pThread, ppEvalResult,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution protected by EvalWaiter mutex.
            HRESULT Status = S_OK;
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->CallParameterizedFunction(pFunc, ArgsTypeCount, ppArgsType, ArgsValueCount, ppArgsValue));
            return S_OK;
        });
}

HRESULT EvalHelpers::FindMethodInModule(const std::string &moduleName, const WSTRING &className,
                                        const WSTRING &methodName, ICorDebugFunction **ppFunction)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(m_sharedDebugInfo->GetModuleWithName(moduleName, &trModule));
    IfFailRet(FindFunction(trModule, className, methodName, ppFunction));
    return S_OK;
}

HRESULT EvalHelpers::TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult)
{
    const std::scoped_lock<std::mutex> lock(m_typeObjectCacheMutex);

    HRESULT Status = S_OK;
    ToRelease<ICorDebugType2> trType2;
    IfFailRet(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2)));

    COR_TYPEID typeID;
    IfFailRet(trType2->GetTypeID(&typeID));

    auto is_same = [&typeID](type_object_t &typeObject)
                   {
                       return typeObject.m_TypeID.token1 == typeID.token1 && typeObject.m_TypeID.token2 == typeID.token2;
                   };
    auto it = std::find_if(m_typeObjectCache.begin(), m_typeObjectCache.end(), is_same);
    if (it == m_typeObjectCache.end())
    {
        return E_FAIL;
    }

    // Move data to begin, so, last used will be on front.
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

    auto is_same = [&typeID](type_object_t &typeObject)
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
        handleType != CorDebugHandleType::HANDLE_STRONG)
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
        m_typeObjectCache.emplace_front(type_object_t{typeID, trHandleValue.Detach()});
    }

    return S_OK;
}

HRESULT EvalHelpers::CreatTypeObjectStaticConstructor(ICorDebugThread *pThread, ICorDebugType *pType,
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

    // Create type object only in case type have static members.
    // Note, for some cases we have static members check outside this method.
    if (DetectStaticMembers && !TypeHaveStaticMembers(pType))
    {
        return S_FALSE;
    }

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ICorDebugType *curType = nullptr; // NOLINT(misc-const-correctness)
        ULONG fetched = 0;
        while (SUCCEEDED(trTypeEnum->Next(1, &curType, &fetched)) && fetched == 1)
        {
            trTypeParams.emplace_back(curType);
        }
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pType->GetClass(&trClass));

    ToRelease<ICorDebugValue> trTypeObject;
    IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, &trTypeObject,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->NewParameterizedObjectNoConstructor(trClass, static_cast<uint32_t>(trTypeParams.size()),
                                                                   reinterpret_cast<ICorDebugType **>(trTypeParams.data())));
            return S_OK;
        }));

    if (et == ELEMENT_TYPE_CLASS)
    {
        const std::scoped_lock<std::mutex> lock(m_trSuppressFinalizeMutex);

        if (m_trSuppressFinalize == nullptr)
        {
            static const std::string assemblyName("System.Private.CoreLib.dll");
            static const WSTRING gcName(W("System.GC"));
            static const WSTRING suppressFinalizeMethodName(W("SuppressFinalize"));
            IfFailRet(FindMethodInModule(assemblyName, gcName, suppressFinalizeMethodName, &m_trSuppressFinalize));
        }

        if (m_trSuppressFinalize == nullptr)
        {
            return E_FAIL;
        }

        // Note, this call must ignore any eval flags.
        IfFailRet(EvalFunction(pThread, m_trSuppressFinalize, &pType, 1, trTypeObject.GetRef(), 1, nullptr, true));
    }

    AddTypeObjectToCache(pType, trTypeObject);

    if (ppTypeObjectResult != nullptr)
    {
        *ppTypeObjectResult = trTypeObject.Detach();
    }

    return S_OK;
}

HRESULT EvalHelpers::GetLiteralValue(ICorDebugThread *pThread, ICorDebugType *pType, ICorDebugModule *pModule,
                                     PCCOR_SIGNATURE pSignatureBlob, ULONG /*sigBlobLength*/, UVCP_CONSTANT pRawValue,
                                     ULONG rawValueLength, ICorDebugValue **ppLiteralValue)
{
    HRESULT Status = S_OK;

    if ((pRawValue == nullptr) || (pThread == nullptr))
    {
        return S_FALSE;
    }

    CorSigUncompressCallingConv(pSignatureBlob);
    CorElementType underlyingType = ELEMENT_TYPE_MAX; // NOLINT(misc-const-correctness)
    CorSigUncompressElementType(pSignatureBlob, &underlyingType);

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    switch (underlyingType)
    {
        case ELEMENT_TYPE_OBJECT:
        {
            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, nullptr, ppLiteralValue));
            break;
        }
        case ELEMENT_TYPE_CLASS:
        {
            // Get token and create null reference
            mdTypeDef tk = mdTypeDefNil;
            CorSigUncompressElementType(pSignatureBlob);
            CorSigUncompressToken(pSignatureBlob, &tk);

            ToRelease<ICorDebugClass> trValueClass;
            IfFailRet(pModule->GetClassFromToken(tk, &trValueClass));

            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, trValueClass, ppLiteralValue));
            break;
        }
        case ELEMENT_TYPE_ARRAY:
        case ELEMENT_TYPE_SZARRAY:
        {
            // Get type name from signature and get its ICorDebugType
            std::string typeName;
            NameForTypeSig(pSignatureBlob, pType, trMDImport, typeName);
            ToRelease<ICorDebugType> trElementType;
            IfFailRet(EvalUtils::GetType(typeName, pThread, m_sharedDebugInfo.get(), &trElementType));

            // We can not directly create null value of specific array type.
            // Instead, we create one element array with element type set to our specific array type.
            // Since array elements are initialized to null, we get our null value from the first array item.

            uint32_t dims = 1;
            uint32_t bounds = 0;
            ToRelease<ICorDebugValue> trTmpArrayValue;
            IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, &trTmpArrayValue,
                [&](ICorDebugEval *pEval) -> HRESULT
                {
                    // Note, this code execution protected by EvalWaiter mutex.
                    ToRelease<ICorDebugEval2> trEval2;
                    IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
                    IfFailRet(trEval2->NewParameterizedArray(trElementType, 1, &dims, &bounds));
                    return S_OK;
                }));

            BOOL isNull = FALSE;
            ToRelease<ICorDebugValue> trUnboxedResult;
            IfFailRet(DereferenceAndUnboxValue(trTmpArrayValue, &trUnboxedResult, &isNull));

            ToRelease<ICorDebugArrayValue> trArray;
            IfFailRet(trUnboxedResult->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArray)));
            IfFailRet(trArray->GetElementAtPosition(0, ppLiteralValue));
            break;
        }
        case ELEMENT_TYPE_GENERICINST:
        {
            // Get type name from signature and get its ICorDebugType
            std::string typeName;
            NameForTypeSig(pSignatureBlob, pType, trMDImport, typeName);
            ToRelease<ICorDebugType> trValueType;
            IfFailRet(EvalUtils::GetType(typeName, pThread, m_sharedDebugInfo.get(), &trValueType));

            // Create value from ICorDebugType
            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(trEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->CreateValueForType(trValueType, ppLiteralValue));
            break;
        }
        case ELEMENT_TYPE_VALUETYPE:
        {
            // Get type token
            mdTypeDef tk = mdTypeDefNil;
            CorSigUncompressElementType(pSignatureBlob);
            CorSigUncompressToken(pSignatureBlob, &tk);

            ToRelease<ICorDebugClass> trValueClass;
            IfFailRet(pModule->GetClassFromToken(tk, &trValueClass));

            // Create value (without calling a constructor)
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, &trValue,
                [&](ICorDebugEval *pEval) -> HRESULT
                {
                    // Note, this code execution protected by EvalWaiter mutex.
                    ToRelease<ICorDebugEval2> trEval2;
                    IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
                    IfFailRet(trEval2->NewParameterizedObjectNoConstructor(trValueClass, 0, nullptr));
                    return S_OK;
                }));

            // Set value
            BOOL isNull = FALSE;
            ToRelease<ICorDebugValue> trEditableValue;
            IfFailRet(DereferenceAndUnboxValue(trValue, &trEditableValue, &isNull));

            ToRelease<ICorDebugGenericValue> trGenericValue;
            IfFailRet(trEditableValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
            IfFailRet(trGenericValue->SetValue(const_cast<void *>(pRawValue))); // NOLINT(cppcoreguidelines-pro-type-const-cast)
            *ppLiteralValue = trValue.Detach();
            break;
        }
        case ELEMENT_TYPE_STRING:
        {
            IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppLiteralValue,
                [&](ICorDebugEval *pEval) -> HRESULT
                {
                    // Note, this code execution protected by EvalWaiter mutex.
                    ToRelease<ICorDebugEval2> trEval2;
                    IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
                    IfFailRet(trEval2->NewStringWithLength(reinterpret_cast<const WCHAR *>(pRawValue), rawValueLength));
                    return S_OK;
                }));

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
            return E_FAIL;
    }
    return S_OK;
}

} // namespace dncdbg
