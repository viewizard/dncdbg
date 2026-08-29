// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/helpers.h"
#include "debugger/evaluator.h"
#include "debugger/evalstackmachine.h"
#include "debugger/valueprint.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <limits>

namespace dncdbg::BreakpointHelpers
{

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

HRESULT IsEnableByCondition(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread,
                            const std::string &condition, std::string &output)
{
    assert(!condition.empty());

    std::string value;
    std::string displayTypeName;
    ToRelease<ICorDebugValue> trResultValue;
    if (FAILED(pEvalStackMachine->EvaluateExpression(pThread, FrameLevel{0}, condition, FormatSpecifier::None,
                                                     nullptr, &trResultValue, nullptr, output)) ||
        FAILED(MetadataHelpers::GetFQDisplayTypeName(trResultValue, displayTypeName)) ||
        FAILED(PrintValue(pThread, pEvaluator, pEvalStackMachine, trResultValue, FormatSpecifier::None, value)))
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
    // In case process was not stopped, GetJMCStatus() could return CORDBG_E_PROCESS_NOT_SYNCHRONIZED or another error code.
    // It is OK, check it as JMC code (pModule have symbols for sure), we will also check JMC status at breakpoint callback itself.
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

    nativeAddress = 0;

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pBreakpoint->GetFunction(&trFunction));
    uint32_t ilOffset = 0;
    IfFailRet(pBreakpoint->GetOffset(&ilOffset));
    ToRelease<ICorDebugCode> trNativeCode;
    IfFailRet(trFunction->GetNativeCode(&trNativeCode));
    CORDB_ADDRESS nativeBaseAddress = 0;
    IfFailRet(trNativeCode->GetAddress(&nativeBaseAddress));

    uint32_t mapElementsCount = 0;
    IfFailRet(trNativeCode->GetILToNativeMapping(0, &mapElementsCount, nullptr));
    if (mapElementsCount == 0)
    {
        return E_FAIL; // Mapping data is unavailable (e.g., lightweight/dynamic methods)
    }

    std::vector<COR_DEBUG_IL_TO_NATIVE_MAP> mapping(mapElementsCount);
    IfFailRet(trNativeCode->GetILToNativeMapping(mapElementsCount, &mapElementsCount, mapping.data()));

    bool found = false;
    uint32_t diff = std::numeric_limits<uint32_t>::max();

    // Search the JIT map to find which native offset corresponds to our IL offset
    for (uint32_t i = 0; i < mapElementsCount; ++i)
    {
        const auto &entry = mapping.at(i);

        // Skip internal CLR runtime markers that do not map to real IL code:
        // - NO_MAPPING (0xffffffff): Code generated by CLR (GC checks, security blocks)
        // - PROLOG     (0xfffffffe): Method initialization code
        // - EPILOG     (0xfffffffd): Method return/cleanup code
        if (entry.ilOffset == static_cast<uint32_t>(NO_MAPPING) ||
            entry.ilOffset == static_cast<uint32_t>(PROLOG) ||
            entry.ilOffset == static_cast<uint32_t>(EPILOG))
        {
            continue;
        }

        // Exact match: The breakpoint IL offset points precisely to an IL boundary
        if (entry.ilOffset == ilOffset)
        {
            nativeAddress = nativeBaseAddress + entry.nativeStartOffset;
            found = true;
            break;
        }

        // Closest match: The breakpoint IL offset most likely points inside an IL range.
        // We keep track of the closest preceding boundary (the 'left' bound of the IL range).
        if (entry.ilOffset <= ilOffset &&
            diff > ilOffset - entry.ilOffset)
        {
            nativeAddress = nativeBaseAddress + entry.nativeStartOffset;
            diff = ilOffset - entry.ilOffset;
            found = true;
        }
    }

    return found ? S_OK : E_FAIL;
}

} // namespace dncdbg::BreakpointHelpers
