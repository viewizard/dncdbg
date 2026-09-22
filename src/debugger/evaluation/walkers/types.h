// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_TYPES_H
#define DEBUGGER_EVALUATION_TYPES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "metadata/sigparse.h"
#include "utils/torelease.h"
#include <functional>
#include <string>
#include <vector>

namespace dncdbg::Walkers
{

struct SetterData
{
    ToRelease<ICorDebugValue> trThisValue;
    ToRelease<ICorDebugType> trPropertyType;
    ToRelease<ICorDebugFunction> trSetterFunction;

    explicit SetterData(ICorDebugValue *pValue, ICorDebugType *pType = nullptr, ICorDebugFunction *pFunction = nullptr)
    {
        Set(pValue, pType, pFunction);
    }

    SetterData(SetterData &setterData)
    {
        Set(setterData.trThisValue.GetPtr(), setterData.trPropertyType.GetPtr(), setterData.trSetterFunction.GetPtr());
    }

    SetterData(SetterData &&) = delete;
    SetterData(const SetterData &) = delete;
    SetterData &operator=(SetterData &&) = delete;
    SetterData &operator=(const SetterData &) = delete;
    ~SetterData() = default;

    void Set(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
    {
        if (pValue != nullptr)
        {
            pValue->AddRef();
        }
        trThisValue = pValue;

        if (pType != nullptr)
        {
            pType->AddRef();
        }
        trPropertyType = pType;

        if (pFunction != nullptr)
        {
            pFunction->AddRef();
        }
        trSetterFunction = pFunction;
    }
};

using GetFunctionCallback = std::function<HRESULT(ICorDebugFunction **)>;
using GetValueCallback = std::function<HRESULT(ICorDebugValue **, std::string *)>;
using ReturnElementType = SigElementType;
using WalkIndexersCallback = std::function<HRESULT(std::vector<SigElementType> &, GetFunctionCallback)>;
using WalkMembersCallback = std::function<HRESULT(ICorDebugType *, bool, const std::string &, const GetValueCallback &,
                                                  SetterData *, std::string *)>;
using WalkMethodsCallback = std::function<HRESULT(bool, const std::string &, ReturnElementType &,
                                                  std::vector<SigElementType> &, uint32_t, GetFunctionCallback)>;
using WalkStackVarsCallback = std::function<HRESULT(const std::string &, const GetValueCallback &)>;

} // namespace dncdbg::Walkers

#endif // DEBUGGER_EVALUATION_TYPES_H
