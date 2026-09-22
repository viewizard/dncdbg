// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "config/config.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/frames.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <list>
#include <sstream>
#include <vector>

namespace dncdbg::Walkers
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

HRESULT GetStaticField(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugType *pType,
                       mdFieldDef fieldDef, ICorDebugValue **ppResultValue)
{
    if (pThread == nullptr)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));

    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    // Detect whether the class is initialized (its static constructor .cctor has run).
    // We read the MethodTable initialization flag directly from the debuggee's memory.
    // COR_TYPEID.token1 is the MethodTable address. MethodTable contains the
    // m_pAuxiliaryData pointer, and MethodTableAuxiliaryData has m_dwFlags, where
    // bit 0 (enum_flag_Initialized = 0x0001) indicates whether the class constructor has run.
    static constexpr DWORD enum_flag_Initialized = 0x0001;

    bool isClassInitialized = true; // Assume initialized by default.

    // Get the MethodTable address via ICorDebugType2::GetTypeID().
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

        // Read the MethodTable to get the m_pAuxiliaryData pointer.
        // The offset of m_pAuxiliaryData within MethodTable depends on the pointer size,
        // so it is computed from the layout below. We rely on m_dwFlags being at offset 0
        // in MethodTableAuxiliaryData, with the Initialized flag in bit 0.

        // Read enough of the MethodTable to reach m_pAuxiliaryData.
        // MethodTable layout (from the runtime sources):
        // - m_dwFlags (DWORD) at offset 0
        // - m_BaseSize (DWORD) at offset 4
        // - m_dwFlags2 (DWORD) at offset 8
        // - m_wNumVirtuals (WORD) at offset 12
        // - m_wNumInterfaces (WORD) at offset 14
        // - m_pParentMethodTable (pointer) at offset 16
        // - m_pModule (pointer) at offset 16 + sizeof(pointer)
        // - m_pAuxiliaryData (pointer) at offset 16 + 2 * sizeof(pointer)
        static constexpr size_t auxDataOffset = (sizeof(DWORD) * 3) + (sizeof(WORD) * 2) + (sizeof(void *) * 2);
        static constexpr size_t readSize = auxDataOffset + sizeof(void *);
        std::array<BYTE, readSize> buffer{0};
        SIZE_T bytesRead = 0;

        if (SUCCEEDED(trProcess->ReadMemory(methodTableAddr, readSize, buffer.data(), &bytesRead)) && bytesRead >= readSize)
        {
            // Get the auxiliary data pointer.
            uintptr_t auxDataAddr = 0;
            std::memcpy(&auxDataAddr, buffer.data() + auxDataOffset, sizeof(uintptr_t));
            if (auxDataAddr != 0)
            {
                // Read m_dwFlags from MethodTableAuxiliaryData (at offset 0).
                DWORD auxFlags = 0;
                if (SUCCEEDED(trProcess->ReadMemory(static_cast<CORDB_ADDRESS>(auxDataAddr), sizeof(DWORD),
                                                    reinterpret_cast<BYTE *>(&auxFlags), &bytesRead)))
                {
                    isClassInitialized = (auxFlags & enum_flag_Initialized) != 0;
                }
            }
        }
    }

    // The class should already be initialized at this point. If it is not, force the
    // static constructor to run as a second chance, with proper error handling.
    if (!isClassInitialized)
    {
        IfFailRet(EvalExec::CreateTypeObject(pThread, pType, nullptr));
    }

    IfFailRet(pType->GetStaticFieldValue(fieldDef, trFrame, ppResultValue));

    return S_OK;
}

} // unnamed namespace

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                    bool provideSetterData, FormatSpecifier specifier, const WalkMembersCallback &cb)
{
    // Same behavior as MS vsdbg and the MSVS C# debugger: don't show enumeration members.
    if (IsEnumeration(pInputValue))
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    bool showInRaw = (specifier & FormatSpecifier::DisplaysInRawMode) != FormatSpecifier::None ||
                     (Config::GetEvalFlags() & Config::EVAL_SHOWRAWVALUES) != 0U;
    bool showHidden = (specifier & FormatSpecifier::DisplaysHiddenMembers) != FormatSpecifier::None;
    bool walkContainer = (specifier & FormatSpecifier::WalkContainerMembers) != FormatSpecifier::None;

    struct WalkValue
    {
        ToRelease<ICorDebugValue> trValue;
        bool isTypeProxyValue = false;
        bool walkContainerMembers = false;

        WalkValue(ICorDebugValue *pValue, bool isTypeProxyValue_, bool walkContainerMembers_ = false)
            : trValue(pValue),
              isTypeProxyValue(isTypeProxyValue_),
              walkContainerMembers(walkContainerMembers_)
        {
        }
    };

    // Queue of fields/properties to process. Includes elements with
    // DebuggerBrowsableState.RootHidden, to unwrap their members during the walk.
    std::list<WalkValue> trWalkQueue;
    pInputValue->AddRef();
    trWalkQueue.emplace_back(pInputValue, false);

    const auto walkNext = [&](ICorDebugValue *pFrontValue, bool isTypeProxyValue, bool walkContainerMembers) -> HRESULT
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
            const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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
        if (!walkContainer &&
            SUCCEEDED(trValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayValue))))
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
                const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
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

        ToRelease<ICorDebugType> trType;
        {
            ToRelease<ICorDebugValue2> trValue2;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
            IfFailRet(trValue2->GetExactType(&trType));
        }
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

            if (!walkContainerMembers && !displayTypeName.empty() && displayTypeName.back() == '?') // System.Nullable<T>
            {
                ToRelease<ICorDebugValue> trValueValue;
                bool hasValue = false;
                IfFailRet(GetNullableValue(trValue, &trValueValue, hasValue));

                if (walkContainer)
                {
                    // Walk the Nullable<T> object itself.
                    pFrontValue->AddRef();
                    trWalkQueue.emplace_back(pFrontValue, false, true);
                }

                if (hasValue)
                {
                    CorElementType elemType = ELEMENT_TYPE_MAX;
                    IfFailRet(trValueValue->GetType(&elemType));

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
                if (walkContainer)
                {
                    // In case of a string, the input value is always a reference (ELEMENT_TYPE_CLASS),
                    // so query the exact type from it to walk the string members.
                    trValue.Free();
                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(pFrontValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    trType.Free();
                    IfFailRet(trValue2->GetExactType(&trType));
                }
                else
                {
                    return S_OK;
                }
            }
            else if (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
            {
                // Create proper System.Array type in order to walk members.
                trType.Free();
                ToRelease<ICorDebugClass> trClass;
                IfFailRet(SystemTypes::GetClass(SystemTypes::SystemType::Array, &trClass));
                ToRelease<ICorDebugClass2> trClass2;
                IfFailRet(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2)));
                IfFailRet(trClass2->GetParameterizedType(ELEMENT_TYPE_CLASS, 0, nullptr, &trType));
            }
            else if (elemType != ELEMENT_TYPE_CLASS && elemType != ELEMENT_TYPE_VALUETYPE)
            {
                return S_OK;
            }

            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef currentTypeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&currentTypeDef));

            if (!showInRaw && isNull == FALSE && !isTypeProxyValue)
            {
                ToRelease<ICorDebugValue> trTypeProxyValue;
                if (SUCCEEDED(TypeProxy::GetDebuggerTypeProxyValue(pThread, trModule, pFrontValue, trType,
                                                                   currentTypeDef, &trTypeProxyValue)))
                {
                    trWalkQueue.emplace_front(trTypeProxyValue.Detach(), true);
                    if (!walkContainer)
                    {
                        return S_OK;
                    }
                }
            }

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            IfFailRet(EvalMetadataHelpers::ForEachFields(trMDImport, currentTypeDef,
                [&](mdFieldDef fieldDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, fieldDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG nameLen = 0;
                    DWORD fieldAttr = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, &fieldAttr,
                                                         nullptr, nullptr, nullptr, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if (isTypeProxyValue && !showHidden &&
                        (fieldAttr & fdFieldAccessMask) != fdPublic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    WSTRING mdName(nameLen, '\0');
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    UVCP_CONSTANT pRawValue = nullptr;
                    ULONG rawValueLength = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, nullptr,
                                                         &pSig, &cbSig, nullptr, &pRawValue, &rawValueLength)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // Remove null terminator that was included in the length
                    if (!mdName.empty() && mdName.back() == '\0')
                    {
                        mdName.pop_back();
                    }

                    // Prevent access to internal compiler-added fields (without a visible name).
                    // They should be accessed by debugger routines only and hidden from the user/IDE.
                    // More about compiler-generated names in the Roslyn sources:
                    // https://github.com/dotnet/roslyn/blob/315c2e149ba7889b0937d872274c33fcbfe9af5f/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNames.cs
                    // Note, uncontrolled access to an internal compiler-added field or its properties may break debugger work.
                    if (!showHidden && MetadataHelpers::IsSynthesizedLocalName(mdName))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const bool isStatic = (fieldAttr & fdStatic);
                    if (isNull == TRUE && !isStatic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(mdName.c_str());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *pFallbackTypeName) -> HRESULT
                    {
                        if (fieldAttr & fdLiteral)
                        {
                            std::string realDisplayTypeName;
                            IfFailRet(EvalExec::CreateLiteralFieldValue(pThread, pSig, pSig + cbSig, pRawValue,
                                                                        rawValueLength, ppResultValue, realDisplayTypeName));

                            if (pFallbackTypeName != nullptr)
                            {
                                *pFallbackTypeName = std::move(realDisplayTypeName);
                            }
                        }
                        else if (fieldAttr & fdStatic)
                        {
                            IfFailRet(GetStaticField(pThread, frameLevel, trType, fieldDef, ppResultValue));
                        }
                        else
                        {
                            // Re-acquire trValue from pFrontValue, since it could be neutered by an eval call in `cb` on a previous iteration.
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
                        return S_OK; // Return success to continue walking.
                    }

                    std::string textWithEval;
                    HasDebuggerAttribute(trMDImport, fieldDef, DebuggerAttribute::Display, textWithEval);

                    IfFailRet(cb(trType, isStatic, name, getValue, nullptr, &textWithEval));
                    if (Status == S_CAN_EXIT)
                    {
                        return S_CAN_EXIT; // Fast exit from the loop.
                    }

                    return S_OK; // Return success to continue walking.
                }));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }
            Status = EvalMetadataHelpers::ForEachProperties(trMDImport, currentTypeDef,
                [&](mdProperty propertyDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, propertyDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG propertyNameLen = 0;
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, &propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, nullptr, nullptr, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    mdMethodDef mdGetter = mdMethodDefNil;
                    mdMethodDef mdSetter = mdMethodDefNil;
                    std::vector<WCHAR> propertyName(propertyNameLen, '\0');
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, propertyName.data(), propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    DWORD getterAttr = 0;
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                          &pSig, &cbSig, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if (isTypeProxyValue && !showHidden &&
                        (getterAttr & mdMemberAccessMask) != mdPublic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    bool isStatic = (getterAttr & mdStatic);
                    if (isNull == TRUE && !isStatic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // A bit hacky, but a fast way to detect an indexer:
                    // an instance property getter that takes arguments is an indexer for sure.
                    // Note, indexers cannot be static in C#, but other .NET languages allow static parameterized properties.
                    uint32_t argCount = 0;
                    if (!isStatic &&
                        SUCCEEDED(GetMethodArgCount(pSig, pSig + cbSig, argCount)) &&
                        argCount > 0)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(propertyName.data());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                    {
                        if (pThread == nullptr)
                        {
                            return E_FAIL;
                        }

                        ToRelease<ICorDebugFunction> trFunc;
                        IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                        return EvalExec::CallFunction(pThread, trFunc, trType.GetPtr(), nullptr,
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
                        return S_OK; // Return success to continue walking.
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
                        SetterData setterData(isStatic ? nullptr : pFrontValue, trType, trFuncSetter);
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

                    return S_OK; // Return success to continue walking.
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
                        EvalExec::CreateTypeObject(pThread, trBaseType, nullptr);
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
        const bool walkContainerMembers = trWalkQueue.front().walkContainerMembers;
        trWalkQueue.pop_front();

        IfFailRet(walkNext(trFrontValue, isTypeProxyValue, walkContainerMembers));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    return S_OK;
}

} // namespace dncdbg::Walkers
