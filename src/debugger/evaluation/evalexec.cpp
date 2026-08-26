// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalexec.h"
#include "debugger/evaluation/evalwaiter.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalhelpers.h"
#include "metadata/corhelpers.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "metadata/sigparse.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>

namespace dncdbg
{

void EvalExec::Cleanup()
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

HRESULT EvalExec::CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString)
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

HRESULT EvalExec::CallFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType *pArgType,
                               std::vector<ToRelease<ICorDebugType>> *pTrMethodGenericTypes,
                               ICorDebugValue **ppArgsValue, uint32_t argsValueCount,
                               FormatSpecifier specifier, ICorDebugValue **ppEvalResult)
{
    assert((ppArgsValue == nullptr && argsValueCount == 0) ||
           (ppArgsValue != nullptr && argsValueCount > 0));

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

HRESULT EvalExec::CallConstructor(ICorDebugThread *pThread, ICorDebugFunction *pConstrFunc,
                                  std::vector<ToRelease<ICorDebugType>> &trTypeParams,
                                  ICorDebugValue **ppArgsValue, uint32_t argsValueCount, ICorDebugValue **ppEvalResult)
{
    assert((ppArgsValue == nullptr && argsValueCount == 0) ||
           (ppArgsValue != nullptr && argsValueCount > 0));

    HRESULT Status = S_OK;

    IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppEvalResult,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));

            // Note, ICorDebugEval2::NewParameterizedObject uses the VALIDATE_POINTER_TO_OBJECT_ARRAY macro
            // unconditionally (unlike CallParameterizedFunction, which guards it with `if (nArgs > 0)`),
            // so rgpArgs must be non-null even when nArgs == 0, otherwise it returns E_INVALIDARG (0x80070057).
            // To satisfy this requirement when there are no arguments, pass a pointer to a local null value.
            ICorDebugValue *emptyArgs = nullptr;
            IfFailRet(trEval2->NewParameterizedObject(pConstrFunc, static_cast<uint32_t>(trTypeParams.size()),
                                                      reinterpret_cast<ICorDebugType **>(trTypeParams.data()),
                                                      argsValueCount, ppArgsValue != nullptr ? ppArgsValue : &emptyArgs));
            return S_OK;
        }));

    return S_OK;
}

HRESULT EvalExec::TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult)
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

HRESULT EvalExec::AddTypeObjectToCache(ICorDebugType *pType, ICorDebugValue *pTypeObject)
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

HRESULT EvalExec::CreateTypeObject(ICorDebugThread *pThread, ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult)
{
    HRESULT Status = S_OK;

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(pType->GetType(&elemType));

    if ((elemType != ELEMENT_TYPE_CLASS && elemType != ELEMENT_TYPE_VALUETYPE) ||
        SUCCEEDED(TryReuseTypeObjectFromCache(pType, ppTypeObjectResult))) // Check cache first, before creating a new type object.
    {
        return S_OK;
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

    if (elemType == ELEMENT_TYPE_CLASS)
    {
        const std::scoped_lock<std::mutex> lock(m_trSuppressFinalizeMutex);

        if (m_trSuppressFinalize == nullptr)
        {
            static const std::string moduleFileName("System.Private.CoreLib.dll");
            static const WSTRING gcTypeName(W("System.GC"));
            static const WSTRING suppressFinalizeMethodName(W("SuppressFinalize"));
            IfFailRet(FindFunctionInModule(pThread, moduleFileName, gcTypeName, suppressFinalizeMethodName, &m_trSuppressFinalize));
            if (m_trSuppressFinalize == nullptr)
            {
                return E_FAIL;
            }
        }

        // Note: this call must ignore any eval flags.
        IfFailRet(CallFunction(pThread, m_trSuppressFinalize, pType, nullptr, trTypeObject.GetRef(),
                               1, FormatSpecifier::ForceEvaluation, nullptr));
    }

    AddTypeObjectToCache(pType, trTypeObject);

    if (ppTypeObjectResult != nullptr)
    {
        *ppTypeObjectResult = trTypeObject.Detach();
    }

    return S_OK;
}

HRESULT EvalExec::CreateArray(ICorDebugThread *pThread, ICorDebugType *pElementType,
                              std::vector<uint32_t> &dimensions, ICorDebugValue **ppEvalResult)
{
    if (dimensions.empty())
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;

    IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppEvalResult,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));

            // The `lowBounds` parameter is not used in `ICorDebugEval2::NewParameterizedArray`; ignore it.
            // https://github.com/dotnet/runtime/blob/cb8ddc8cac0182e932903d921bd09300be7ae2a6/src/coreclr/debug/di/rsthread.cpp#L10178
            IfFailRet(trEval2->NewParameterizedArray(pElementType, static_cast<uint32_t>(dimensions.size()),
                                                     dimensions.data(), nullptr));
            return S_OK;
        }));

    return S_OK;
}

HRESULT EvalExec::CreateLiteralFieldValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, UVCP_CONSTANT pRawValue,
                                          ULONG rawValueLength, ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName)
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

    return CreateLiteralValueImpl(pThread, pSig, pSigEnd, underlyingType, pRawValue, rawValueLength, ppLiteralValue, realDisplayTypeName);
}

HRESULT EvalExec::CreateLiteralLocalValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                          ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName)
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
        rawValueLength > 0 &&
        *reinterpret_cast<const uint8_t *>(pRawValue) == nullStringMarker)
    {
        rawValueLength = 0;
    }

    return CreateLiteralValueImpl(pThread, pSig, pSigEnd, underlyingType, pRawValue, rawValueLength,
                                  ppLiteralValue, realDisplayTypeName, true);
}

HRESULT EvalExec::CreateLiteralValueImpl(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                         CorElementType underlyingType, UVCP_CONSTANT pRawValue, ULONG rawValueLength,
                                         ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName,
                                         bool valueInlineInSig)
{
    if (pThread == nullptr || pSig == nullptr || pSigEnd == nullptr || ppLiteralValue == nullptr)
    {
        return E_POINTER;
    }

    *ppLiteralValue = nullptr;
    realDisplayTypeName.clear();
    HRESULT Status = S_OK;

    auto createByTypeDef = [&](ICorDebugModule *pModule, mdTypeDef typeDef) -> HRESULT
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

    // Create by fully-qualified metadata (FQMD) name.
    auto createByFQMDName = [&](const WSTRING &wName) -> HRESULT
    {
        if (wName.empty())
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
                if (FAILED(trMDImportDef->FindTypeDefByName(wName.c_str(), mdTypeDefNil, &typeDef)))
                {
                    return S_OK; // Return success to continue walking.
                }

                IfFailRet(createByTypeDef(pModule, typeDef));
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
        {
            IfFailRet(createNullObjectValue());
            break;
        }
        case ELEMENT_TYPE_ARRAY:
        case ELEMENT_TYPE_SZARRAY:
        {
            ToRelease<ICorDebugFrame> trFrame;
            IfFailRet(pThread->GetActiveFrame(&trFrame));
            ToRelease<ICorDebugFunction> trFunction;
            IfFailRet(trFrame->GetFunction(&trFunction));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trFunction->GetModule(&trModule));
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            std::list<std::string> args;
            SigElementType sigType;
            pSig--; // step back to the element type byte so ParseElementType() can re-read it
            if (SUCCEEDED(ParseElementType(trMDImport, pSig, pSigEnd, 0, sigType, &args, true)))
            {
                realDisplayTypeName = MetadataHelpers::ConvertMetadataToDisplayName(sigType.metadataTypeName, &args);
            }

            IfFailRet(createNullObjectValue());
            break;
        }
        case ELEMENT_TYPE_VALUETYPE:
        {
            mdToken typeToken = mdTokenNil;
            IfFailRet(CorSigUncompressToken_EndPtr(pSig, pSigEnd, typeToken));

            if (valueInlineInSig)
            {
                pRawValue = pSig;
                // Note, rawValueLength is not updated here, since it is never read for local constants.
            }

            ToRelease<ICorDebugFrame> trFrame;
            IfFailRet(pThread->GetActiveFrame(&trFrame));
            ToRelease<ICorDebugFunction> trFunction;
            IfFailRet(trFrame->GetFunction(&trFunction));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trFunction->GetModule(&trModule));
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            std::string metadataName;
            MetadataHelpers::GetFQMDTypeNameByToken(typeToken, trMDImport, metadataName);
            const bool decimalConstant = (metadataName == "System.Decimal");

            ToRelease<ICorDebugClass> trClass;
            if (TypeFromToken(typeToken) == mdtTypeDef)
            {
                IfFailRet(trModule->GetClassFromToken(typeToken, &trClass));
            }
            else if (TypeFromToken(typeToken) == mdtTypeRef)
            {
                // Note, IMetaDataImport::GetTypeRefProps() returns a fully-qualified name.
                ULONG nameSize = 0;
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, nullptr, 0, &nameSize));
                WSTRING wName(nameSize, '\0');
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, wName.data(), nameSize, nullptr));

                IfFailRet(Modules::ForEachModule(pThread,
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
                        if (FAILED(trMDImportDef->FindTypeDefByName(wName.c_str(), mdTypeDefNil, &typeDef)))
                        {
                            return S_OK; // Return success to continue walking.
                        }

                        IfFailRet(pModule->GetClassFromToken(typeDef, &trClass));

                        return S_CAN_EXIT; // Fast exit from the loop.
                    }));
            }
            else if (TypeFromToken(typeToken) == mdtTypeSpec)
            {
                return E_NOTIMPL;
            }
            else
            {
                return E_INVALIDARG;
            }

            if (trClass == nullptr)
            {
                return E_INVALIDARG;
            }

            ToRelease<ICorDebugValue> trBoxedValue;
            IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, &trBoxedValue,
                [&](ICorDebugEval *pEval) -> HRESULT
                {
                    // Note, this code execution is protected by EvalWaiter mutex.
                    ToRelease<ICorDebugEval2> trEval2;
                    IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
                    IfFailRet(trEval2->NewParameterizedObjectNoConstructor(trClass, 0, nullptr));
                    return S_OK;
                }));

            BOOL isNull = FALSE;
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(DereferenceAndUnboxValue(trBoxedValue, &trValue, &isNull));

            ToRelease<ICorDebugGenericValue> trGenericValue;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));

            // Note, for local constants Roslyn serializes System.Decimal in the
            // DecimalConstantAttribute format (13 bytes: 1 scale byte + 3x uint32
            // lo/mid/hi), not as the raw 16-byte in-memory struct (flags + hi + lo + mid).
            const void *pSetValueData = pRawValue;
            // System.Decimal in-memory size: 4 x uint32 (flags, hi, lo, mid).
            static constexpr size_t decimalMemSize = 16;
            std::array<uint8_t, decimalMemSize> decimalMem{};
            if (valueInlineInSig && decimalConstant)
            {
                // DecimalConstantAttribute blob layout:
                //   byte 0:     packed scale byte (sign in bit 7, scale in bits 0-6)
                //   bytes 1-4:  lo  (uint32 LE)
                //   bytes 5-8:  mid (uint32 LE)
                //   bytes 9-12: hi  (uint32 LE)
                // In-memory System.Decimal layout (matches DotNetDecimal):
                //   flags (uint32): scale in bits 16-23, sign in bit 31
                //   hi    (uint32)
                //   lo    (uint32)
                //   mid   (uint32)
                // Scale-byte decoding (sign in bit 7, scale in bits 0-6).
                static constexpr uint32_t scaleValueMask = 0x7F;
                static constexpr uint32_t scaleSignMask = 0x80;
                // Flags uint32 encoding (scale in bits 16-23, sign in bit 31).
                static constexpr uint32_t flagsScaleShift = 16;
                static constexpr uint32_t flagsSignBit = 0x80000000U;
                // DecimalConstantAttribute blob byte offsets (LE uint32 after the scale byte).
                static constexpr size_t blobOffLo = 1;
                static constexpr size_t blobOffMid = 5;
                static constexpr size_t blobOffHi = 9;
                // In-memory System.Decimal field byte offsets.
                static constexpr size_t memOffFlags = 0;
                static constexpr size_t memOffHi = 4;
                static constexpr size_t memOffLo = 8;
                static constexpr size_t memOffMid = 12;

                const auto *blob = static_cast<const uint8_t *>(pRawValue);
                const uint8_t scaleByte = blob[0];
                const uint32_t scale = scaleByte & scaleValueMask;
                const bool isNegative = (scaleByte & scaleSignMask) != 0;
                uint32_t flags = (scale << flagsScaleShift);
                if (isNegative)
                {
                    flags |= flagsSignBit;
                }
                uint32_t lo = 0;
                uint32_t mid = 0;
                uint32_t hi = 0;
                std::memcpy(&lo, blob + blobOffLo, sizeof(uint32_t));
                std::memcpy(&mid, blob + blobOffMid, sizeof(uint32_t));
                std::memcpy(&hi, blob + blobOffHi, sizeof(uint32_t));
                // Pack into the in-memory layout: flags, hi, lo, mid.
                std::memcpy(decimalMem.data() + memOffFlags, &flags, sizeof(uint32_t));
                std::memcpy(decimalMem.data() + memOffHi, &hi, sizeof(uint32_t));
                std::memcpy(decimalMem.data() + memOffLo, &lo, sizeof(uint32_t));
                std::memcpy(decimalMem.data() + memOffMid, &mid, sizeof(uint32_t));
                pSetValueData = decimalMem.data();
            }

            IfFailRet(trGenericValue->SetValue(const_cast<void *>(pSetValueData))); // NOLINT(cppcoreguidelines-pro-type-const-cast)
            *ppLiteralValue = trValue.Detach();
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
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            if (TypeFromToken(typeToken) == mdtTypeDef)
            {
                IfFailRet(createByTypeDef(trModule, typeToken));
            }
            else if (TypeFromToken(typeToken) == mdtTypeRef)
            {
                // Note, IMetaDataImport::GetTypeRefProps() returns a fully-qualified name.
                ULONG nameSize = 0;
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, nullptr, 0, &nameSize));
                WSTRING wName(nameSize, '\0');
                IfFailRet(trMDImport->GetTypeRefProps(typeToken, nullptr, wName.data(), nameSize, nullptr));
                IfFailRet(createByFQMDName(wName));
            }
            else if (TypeFromToken(typeToken) == mdtTypeSpec)
            {
                PCCOR_SIGNATURE pTypeSig = nullptr;
                ULONG cbSig = 0;
                IfFailRet(trMDImport->GetTypeSpecFromToken(typeToken, &pTypeSig, &cbSig));
                SigElementType sigType;
                std::list<std::string> args;
                if (SUCCEEDED(ParseElementType(trMDImport, pTypeSig, pTypeSig + cbSig, 0, sigType, &args, true)))
                {
                    realDisplayTypeName = MetadataHelpers::ConvertMetadataToDisplayName(sigType.metadataTypeName, &args);
                }

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
                IfFailRet(createByFQMDName(W("System.String")));
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

HRESULT EvalExec::CreateValueType(ICorDebugThread *pThread, ICorDebugClass *pValueTypeClass,
                                  void *valueData, ICorDebugValue **ppValue)
{
    HRESULT Status = S_OK;
    // Create value (without calling a constructor)
    IfFailRet(m_sharedEvalWaiter->WaitEvalResult(pThread, ppValue,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->NewParameterizedObjectNoConstructor(pValueTypeClass, 0, nullptr));
            return S_OK;
        }));

    if (valueData == nullptr)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue> trEditableValue;
    IfFailRet(DereferenceAndUnboxValue(*ppValue, &trEditableValue, nullptr));

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trEditableValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    return trGenericValue->SetValue(valueData);
}

} // namespace dncdbg
