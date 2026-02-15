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

    ToRelease<ICorDebugFunction> pFunction1;
    ToRelease<ICorDebugFunction> pFunction2;
    IfFailRet(pBreakpoint1->GetFunction(&pFunction1));
    IfFailRet(pBreakpoint2->GetFunction(&pFunction2));

    mdMethodDef methodDef1 = mdMethodDefNil;
    mdMethodDef methodDef2 = mdMethodDefNil;
    IfFailRet(pFunction1->GetToken(&methodDef1));
    IfFailRet(pFunction2->GetToken(&methodDef2));

    if (methodDef1 != methodDef2)
    {
        return S_FALSE;
    }

    ToRelease<ICorDebugModule> pModule1;
    ToRelease<ICorDebugModule> pModule2;
    IfFailRet(pFunction1->GetModule(&pModule1));
    IfFailRet(pFunction2->GetModule(&pModule2));

    CORDB_ADDRESS modAddress1 = 0;
    IfFailRet(pModule1->GetBaseAddress(&modAddress1));
    CORDB_ADDRESS modAddress2 = 0;
    IfFailRet(pModule2->GetBaseAddress(&modAddress2));

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

    ToRelease<ICorDebugProcess> iCorProcess;
    IfFailRet(pThread->GetProcess(&iCorProcess));

    Variable variable; // NOLINT(misc-const-correctness)
    if (FAILED(Status = pVariables->Evaluate(iCorProcess, frameId, condition, variable, output)))
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
    ToRelease<ICorDebugFunction> iCorFunction;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &iCorFunction));
    ToRelease<ICorDebugFunction2> iCorFunction2;
    IfFailRet(iCorFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&iCorFunction2)));
    BOOL JMCStatus = FALSE;
    // In case process was not stopped, GetJMCStatus() could return CORDBG_E_PROCESS_NOT_SYNCHRONIZED or another error code.
    // It is OK, check it as JMC code (pModule have symbols for sure), we will also check JMC status at breakpoint callback itself.
    if (FAILED(iCorFunction2->GetJMCStatus(&JMCStatus)))
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
        ToRelease<IUnknown> iUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &iUnknown));
        ToRelease<IMetaDataImport> iMD;
        IfFailRet(iUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&iMD)));

        if (HasAttribute(iMD, methodToken, DebuggerAttribute::Hidden))
        {
            return S_OK; // need skip breakpoint
        }
    }

    return S_FALSE; // don't skip breakpoint
}

} // namespace dncdbg::BreakpointUtils
