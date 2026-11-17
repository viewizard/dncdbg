// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evaluation/evalhelpers/value.h"
#include "debuginfo/debuginfo.h"
#include "metadata/enumerations.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dncdbg::Walkers
{

namespace
{

// A single compiler-generated display class (closure) being walked.
// Frames are kept on an explicit stack instead of recursing into nested display classes.
struct DisplayClassFrame
{
    // Owns the nested display class value; empty for the root frame, whose value is owned by the caller.
    ToRelease<ICorDebugValue> trOwnedValue;
    // The input value of this display class: borrowed for the root frame, owned via trOwnedValue for nested frames.
    ICorDebugValue *pInputValue = nullptr;
    ToRelease<ICorDebugClass> trClass;
    std::vector<mdFieldDef> fieldDefs;
    std::size_t nextFieldIndex = 0;
};

// Pushes a frame for the display class of pValue. Null values are skipped.
HRESULT PushDisplayClassFrame(IMetaDataImport *pMDImport, ICorDebugValue *pValue,
                              ToRelease<ICorDebugValue> trOwnedValue, std::vector<DisplayClassFrame> &frames)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(pValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));

    DisplayClassFrame frame;
    frame.trOwnedValue = std::move(trOwnedValue);
    frame.pInputValue = pValue;
    frame.trClass = std::move(trClass);
    IfFailRet(MetadataEnumerations::ForEachField(pMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            frame.fieldDefs.push_back(fieldDef);
            return S_OK;
        }));

    frames.push_back(std::move(frame));
    return S_OK;
}

// Gets the field value of the display class. The value is dereferenced again on every call,
// since it could be neutered by an eval call in `cb` on a previous iteration.
HRESULT GetDisplayClassFieldValue(const DisplayClassFrame &frame, mdFieldDef fieldDef, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(frame.pInputValue, &trValue));
    ToRelease<ICorDebugObjectValue> trObjValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
    IfFailRet(trObjValue->GetFieldValue(frame.trClass, fieldDef, ppResultValue));
    return S_OK;
}

} // unnamed namespace

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                 std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef,
                                 ICorDebugModule *pModule, const WalkStackVarsCallback &cb)
{
    HRESULT Status = S_OK;
    std::vector<DisplayClassFrame> frames;
    IfFailRet(PushDisplayClassFrame(pMDImport, pInputValue, ToRelease<ICorDebugValue>(), frames));

    // Reused for every field to avoid a heap allocation per field.
    WSTRING mdName;
    while (!frames.empty())
    {
        DisplayClassFrame &frame = frames.back();
        if (frame.nextFieldIndex >= frame.fieldDefs.size())
        {
            frames.pop_back();
            continue;
        }

        const mdFieldDef fieldDef = frame.fieldDefs.at(frame.nextFieldIndex);
        ++frame.nextFieldIndex;

        ULONG nameLen = 0;
        DWORD fieldAttr = 0;
        IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                           &fieldAttr, nullptr, nullptr, nullptr, nullptr, nullptr));
        if ((fieldAttr & fdStatic) != 0 || (fieldAttr & fdLiteral) != 0)
        {
            continue; // Continue walking.
        }

        // Zero-fill the reused buffer, so no characters from a previous (longer) name remain.
        mdName.assign(nameLen, '\0');
        if (FAILED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue; // Continue walking.
        }
        // Remove null terminator that was included in the length.
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
        {
            return GetDisplayClassFieldValue(frame, fieldDef, ppResultValue);
        };

        const MetadataHelpers::GeneratedNameKind generatedNameKind = MetadataHelpers::GetLocalOrFieldNameKind(mdName);
        if (generatedNameKind == MetadataHelpers::GeneratedNameKind::DisplayClassLocalOrField)
        {
            // The nested display class is walked before continuing with the remaining fields of the current class.
            // Note: `frame` is invalidated by the push below, so it must not be used afterwards.
            ToRelease<ICorDebugValue> trDisplayClassValue;
            IfFailRet(getValue(&trDisplayClassValue, nullptr));
            // Take the raw pointer before the move: argument evaluation order is unspecified.
            ICorDebugValue *pDisplayClassValue = trDisplayClassValue.GetPtr();
            IfFailRet(PushDisplayClassFrame(pMDImport, pDisplayClassValue, std::move(trDisplayClassValue), frames));
        }
        else if (generatedNameKind == MetadataHelpers::GeneratedNameKind::HoistedLocalField)
        {
            // Check that the hoisted local is in scope.
            // Note: if this check fails for any reason, ignore it and show the variable anyway, since it is not a fatal error.
            int32_t index = 0;
            if (SUCCEEDED(MetadataHelpers::TryParseSlotIndex(mdName, index)) && index >= 0 &&
                !DebugInfo::IsHoistedLocalInScope(pModule, methodDef, currentIlOffset, static_cast<uint32_t>(index)))
            {
                continue; // Continue walking.
            }

            if (usedNames.find(mdName) != usedNames.cend())
            {
                continue; // Continue walking.
            }

            WSTRING wLocalName;
            if (FAILED(MetadataHelpers::TryParseGeneratedName(mdName, wLocalName)))
            {
                continue; // Continue walking.
            }

            IfFailRet(cb(to_utf8(wLocalName.c_str()), getValue));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT; // Fast exit from the loop.
            }
            usedNames.insert(std::move(wLocalName));
        }
        // Ignore any other compiler-generated fields, show only normal fields.
        else if (!MetadataHelpers::IsSynthesizedLocalName(mdName) &&
                 usedNames.find(mdName) == usedNames.cend())
        {
            IfFailRet(cb(to_utf8(mdName.c_str()), getValue));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT; // Fast exit from the loop.
            }
            usedNames.insert(mdName);
        }
    }

    return S_OK;
}

} // namespace dncdbg::Walkers
