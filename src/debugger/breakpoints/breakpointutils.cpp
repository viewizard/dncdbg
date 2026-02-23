// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpointutils.h"
#include "debugger/variables.h"
#include "metadata/attributes.h"
#include "utils/torelease.h"

namespace dncdbg::BreakpointUtils
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

HRESULT IsEnableByCondition(const std::string &condition, Variables *pVariables, ICorDebugThread *pThread, std::string &output)
{
    if (condition.empty())
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    DWORD threadId = 0;
    IfFailRet(pThread->GetID(&threadId));
    const FrameId frameId(ThreadId{threadId}, FrameLevel{0});

    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pThread->GetProcess(&trProcess));

    Variable variable; // NOLINT(misc-const-correctness)
    if (FAILED(Status = pVariables->Evaluate(trProcess, frameId, condition, variable, output)))
    {
        if (output.empty())
        {
            output = "unknown error";
        }

        return Status;
    }
    if (variable.type != "bool")
    {
        if (output.empty())
        {
            output = "The breakpoint condition must evaluate to a boolean operation, result type is " + variable.type;
        }

        return E_FAIL;
    }

    if (variable.value != "true")
    {
        return S_FALSE;
    }

    return S_OK;
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
        return S_OK; // need skip breakpoint
    }

    // Care about attributes for "JMC disabled" case.
    if (!justMyCode)
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        if (HasAttribute(trMDImport, methodToken, DebuggerAttribute::Hidden))
        {
            return S_OK; // need skip breakpoint
        }
    }

    return S_FALSE; // don't skip breakpoint
}

} // namespace dncdbg::BreakpointUtils
