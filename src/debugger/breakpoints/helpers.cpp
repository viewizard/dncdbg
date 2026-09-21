// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/helpers.h"
#include "debugger/evalstackmachine.h"
#include "debugger/valueprint.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <cassert>
#include <mutex>
#include <unordered_map>

namespace dncdbg::BreakpointHelpers
{

namespace
{

struct BreakpointLocation
{
    CORDB_ADDRESS modAddress{0};
    uint32_t methodToken{0};
    uint32_t ilOffset{0};

    BreakpointLocation(CORDB_ADDRESS modAddress_, uint32_t methodToken_, uint32_t ilOffset_)
        : modAddress(modAddress_),
          methodToken(methodToken_),
          ilOffset(ilOffset_)
    {
    }

    bool operator==(const BreakpointLocation &other) const
    {
        return modAddress == other.modAddress &&
               methodToken == other.methodToken &&
               ilOffset == other.ilOffset;
    }
};

struct BreakpointLocationHash
{
    std::size_t operator()(const BreakpointLocation &key) const
    {
        const std::size_t h1 = std::hash<CORDB_ADDRESS>{}(key.modAddress);
        const std::size_t h2 = std::hash<uint32_t>{}(key.methodToken);
        const std::size_t h3 = std::hash<uint32_t>{}(key.ilOffset);
        // Combine hashes using XOR and bit shifting (similar to boost::hash_combine)
        return h1 ^ (h2 << 1U) ^ (h3 << 2U);
    }
};

struct BreakpointData
{
    ToRelease<ICorDebugFunctionBreakpoint> trBreakpoint;
    size_t refCount{0};

    BreakpointData(ICorDebugFunctionBreakpoint *pBreakpoint, size_t initialCount)
        : trBreakpoint(pBreakpoint),
          refCount(initialCount)
    {
    }

    BreakpointData(BreakpointData &&) = default;
    BreakpointData(const BreakpointData &) = delete;
    BreakpointData &operator=(BreakpointData &&) = default;
    BreakpointData &operator=(const BreakpointData &) = delete;
    ~BreakpointData() = default;
};

std::mutex &GetManagedBreakpointsMutex()
{
    static std::mutex managedBreakpointsMutex;
    return managedBreakpointsMutex;
}

using mbp_t = std::unordered_map<BreakpointLocation, BreakpointData, BreakpointLocationHash>;
mbp_t &GetManagedBreakpoints()
{
    static mbp_t managedBreakpoints;
    return managedBreakpoints;
}

} // unnamed namespace

HRESULT IsSameFunctionBreakpoint(ICorDebugFunctionBreakpoint *pBreakpoint1, ICorDebugFunctionBreakpoint *pBreakpoint2)
{
    HRESULT Status = S_OK;

    if ((pBreakpoint1 == nullptr) || (pBreakpoint2 == nullptr))
    {
        return E_FAIL;
    }

    uint32_t nOffset1 = 0;
    uint32_t nOffset2 = 0;
    IfFailRet(pBreakpoint1->GetOffset(&nOffset1));
    IfFailRet(pBreakpoint2->GetOffset(&nOffset2));

    if (nOffset1 != nOffset2)
    {
        return S_FALSE;
    }

    ToRelease<ICorDebugFunction> trFunction1;
    ToRelease<ICorDebugFunction> trFunction2;
    IfFailRet(pBreakpoint1->GetFunction(&trFunction1));
    IfFailRet(pBreakpoint2->GetFunction(&trFunction2));

    mdMethodDef methodDef1 = mdMethodDefNil;
    mdMethodDef methodDef2 = mdMethodDefNil;
    IfFailRet(trFunction1->GetToken(&methodDef1));
    IfFailRet(trFunction2->GetToken(&methodDef2));

    if (methodDef1 != methodDef2)
    {
        return S_FALSE;
    }

    ToRelease<ICorDebugModule> trModule1;
    ToRelease<ICorDebugModule> trModule2;
    IfFailRet(trFunction1->GetModule(&trModule1));
    IfFailRet(trFunction2->GetModule(&trModule2));

    CORDB_ADDRESS modAddress1 = 0;
    IfFailRet(trModule1->GetBaseAddress(&modAddress1));
    CORDB_ADDRESS modAddress2 = 0;
    IfFailRet(trModule2->GetBaseAddress(&modAddress2));

    if (modAddress1 != modAddress2)
    {
        return S_FALSE;
    }

    return S_OK;
}

HRESULT GetFunctionBreakpointModAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &modAddress)
{
    HRESULT Status = S_OK;

    if (pBreakpoint == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pBreakpoint->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    return S_OK;
}

HRESULT IsEnableByCondition(ICorDebugThread *pThread, const std::string &condition, std::string &output)
{
    assert(!condition.empty());

    std::string value;
    std::string displayTypeName;
    ToRelease<ICorDebugValue> trResultValue;
    if (FAILED(EvalStackMachine::EvaluateExpression(pThread, FrameLevel{0}, condition, FormatSpecifier::None,
                                                    nullptr, &trResultValue, nullptr, output)) ||
        FAILED(MetadataHelpers::GetFQDisplayTypeName(trResultValue, displayTypeName)) ||
        FAILED(PrintValue(pThread, trResultValue, FormatSpecifier::None, value)))
    {
        if (output.empty())
        {
            output = "unknown error";
        }

        return S_OK; // some evaluation issue - ignore condition, stop at breakpoint
    }
    if (displayTypeName != "bool")
    {
        if (output.empty())
        {
            output = "The breakpoint condition must evaluate to a boolean operation, result type is " + displayTypeName;
        }

        return S_OK; // wrong type - ignore condition, stop at breakpoint
    }

    return value == "true" ? S_OK : S_FALSE;
}

HRESULT SkipBreakpoint(ICorDebugModule *pModule, mdMethodDef methodToken, bool justMyCode)
{
    HRESULT Status = S_OK;

    // Skip breakpoints outside of code with loaded PDB (see JMC setup during module load).
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunction));
    ToRelease<ICorDebugFunction2> trFunction2;
    IfFailRet(trFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&trFunction2)));
    BOOL JMCStatus = FALSE;
    // In case the process was not stopped, GetJMCStatus() could return CORDBG_E_PROCESS_NOT_SYNCHRONIZED or another error code.
    // It is OK, check it as JMC code (pModule has symbols for sure); we will also check the JMC status at the breakpoint callback itself.
    if (FAILED(trFunction2->GetJMCStatus(&JMCStatus)))
    {
        JMCStatus = TRUE;
    }
    if (JMCStatus == FALSE)
    {
        return S_SKIP;
    }

    // Care about attributes for "JMC disabled" case.
    if (!justMyCode)
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        if (HasAttribute(trMDImport, methodToken, DebuggerAttribute::GetHidden()))
        {
            return S_SKIP;
        }
    }

    return S_OK;
}

HRESULT GetBreakpointNativeAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &nativeAddress)
{
    if (pBreakpoint == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pBreakpoint->GetFunction(&trFunction));
    uint32_t ilOffset = 0;
    IfFailRet(pBreakpoint->GetOffset(&ilOffset));
    return MetadataHelpers::GetNativeAddress(trFunction, ilOffset, nativeAddress);
}

HRESULT ActivateManagedBreakpoint(CORDB_ADDRESS modAddress, uint32_t methodToken, uint32_t ilOffset,
                                  ICorDebugModule *pModule, ICorDebugFunctionBreakpoint **ppFuncBreakpoint)
{
    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());

    mbp_t &managedBreakpoints = GetManagedBreakpoints();

    const auto find = managedBreakpoints.find({modAddress, methodToken, ilOffset});
    if (find != managedBreakpoints.cend())
    {
        find->second.trBreakpoint->AddRef();
        find->second.refCount++;
        *ppFuncBreakpoint = find->second.trBreakpoint;
        return S_OK;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunc));
    ToRelease<ICorDebugCode> trCode;
    IfFailRet(trFunc->GetILCode(&trCode));
    IfFailRet(trCode->CreateBreakpoint(ilOffset, ppFuncBreakpoint));
    IfFailRet((*ppFuncBreakpoint)->Activate(TRUE));

    (*ppFuncBreakpoint)->AddRef();
    managedBreakpoints.emplace(BreakpointLocation(modAddress, methodToken, ilOffset), BreakpointData(*ppFuncBreakpoint, 2));

    return S_OK;
}

HRESULT DeactivateManagedBreakpoint(ToRelease<ICorDebugFunctionBreakpoint> &trFuncBreakpoint)
{
    if (trFuncBreakpoint == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;

    uint32_t ilOffset = 0;
    IfFailRet(trFuncBreakpoint->GetOffset(&ilOffset));
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFuncBreakpoint->GetFunction(&trFunction));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());

    mbp_t &managedBreakpoints = GetManagedBreakpoints();

    const auto find = managedBreakpoints.find({modAddress, methodToken, ilOffset});
    if (find == managedBreakpoints.cend())
    {
        return E_FAIL;
    }

    trFuncBreakpoint.Free();
    find->second.refCount--;

    assert(find->second.refCount >= 1);

    if (find->second.refCount == 1)
    {
        find->second.trBreakpoint->Activate(FALSE);
        managedBreakpoints.erase(find);
    }
    return S_OK;
}

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetManagedBreakpointsMutex());
    GetManagedBreakpoints().clear();
}

} // namespace dncdbg::BreakpointHelpers
