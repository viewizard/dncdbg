// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpointutils.h"
#include "debugger/evaluator.h"
#include "debugger/evalstackmachine.h"
#include "debugger/valueprint.h"
#include "metadata/attributes.h"
#include "metadata/typeprinter.h"
#include "utils/hresult.h"
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
    std::string type;
    ToRelease<ICorDebugValue> trResultValue;
    if (FAILED(pEvalStackMachine->EvaluateExpression(pThread, FrameLevel{0}, condition, &trResultValue, output)) ||
        FAILED(TypePrinter::GetTypeOfValue(trResultValue, type)) ||
        FAILED(PrintValue(pThread, pEvaluator, trResultValue, value)))
    {
        if (output.empty())
        {
            output = "unknown error";
        }

        return S_OK; // some evaluation issue - ignore condition, stop at breakpoint
    }
    if (type != "bool")
    {
        if (output.empty())
        {
            output = "The breakpoint condition must evaluate to a boolean operation, result type is " + type;
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

        if (HasAttribute(trMDImport, methodToken, DebuggerAttribute::Hidden))
        {
            return S_SKIP;
        }
    }

    return S_OK;
}

void CreateMessageParts(const std::string &logMessage, std::vector<std::pair<std::string, bool>> &logMessageParts)
{
    size_t pos = 0;
    size_t prevPos = 0;

    while ((pos = logMessage.find('{', prevPos)) != std::string::npos)
    {
        // Add text before the '{' (if any) as literal text.
        if (pos > prevPos)
        {
            logMessageParts.emplace_back(logMessage.substr(prevPos, pos - prevPos), false);
        }

        // Find the matching closing '}' by counting brace depth.
        size_t endPos = pos + 1;
        int braceDepth = 1;
        while (endPos < logMessage.length() && braceDepth > 0)
        {
            if (logMessage.at(endPos) == '{')
            {
                braceDepth++;
            }
            else if (logMessage.at(endPos) == '}')
            {
                braceDepth--;
            }
            endPos++;
        }

        if (braceDepth > 0)
        {
            // No matching closing brace found, treat from '{' to end as literal text.
            logMessageParts.emplace_back(logMessage.substr(pos), false);
            prevPos = logMessage.length();
            break;
        }

        // Add the expression inside braces (without the braces themselves) as expression.
        // endPos points to position after the matching '}', so expression is [pos+1, endPos-1).
        logMessageParts.emplace_back(logMessage.substr(pos + 1, endPos - pos - 2), true);

        prevPos = endPos;
    }

    // Add remaining text after the last '}' (or entire string if no braces found) as literal text.
    if (prevPos < logMessage.length())
    {
        logMessageParts.emplace_back(logMessage.substr(prevPos), false);
    }
}

void BuildTraceMessage(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread,
                       const std::vector<std::pair<std::string, bool>> &logMessageParts, std::string &message)
{
    // Build the final message by evaluating expressions.
    for (const auto &[text, isExpression] : logMessageParts)
    {
        if (!isExpression)
        {
            // Literal text - append directly.
            message += text;
        }
        else
        {
            // Expression - evaluate it.
            std::string value;
            std::string output;
            ToRelease<ICorDebugValue> trResultValue;
            if (SUCCEEDED(pEvalStackMachine->EvaluateExpression(pThread, FrameLevel{0}, text, &trResultValue, output)) &&
                SUCCEEDED(PrintValue(pThread, pEvaluator, trResultValue, value)))
            {
                message += value;
            }
            else
            {
                if (!output.empty())
                {
                    message += "{" + output + "}";
                }
                else
                {
                    message += "{unknown error}";
                }
            }
        }
    }

    message += '\n';
}

} // namespace dncdbg::BreakpointUtils
