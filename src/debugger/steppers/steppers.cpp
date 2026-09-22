// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/steppers/steppers.h"
#include "config/config.h"
#include "debugger/steppers/stepper_async.h"
#include "debugger/steppers/stepper_simple.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdb.h"
#include "metadata/attributes.h"
#include "types/types.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <unordered_set>
#include <vector>

namespace dncdbg::Steppers
{

namespace
{

StepType &GetInitialStepType()
{
    static StepType initialStepType{StepType::STEP_OVER};
    return initialStepType;
}

PDB::SequencePoint &GetStepStartSP()
{
    static PDB::SequencePoint stepStartSP;
    return stepStartSP;
}

// https://docs.microsoft.com/en-us/visualstudio/debugger/navigating-through-code-with-the-debugger?view=vs-2019#BKMK_Step_into_properties_and_operators_in_managed_code
// The debugger steps over properties and operators in managed code by default. In most cases, this provides a better debugging experience.
bool &GetStepFiltering()
{
    static bool stepFiltering{true};
    return stepFiltering;
}

// Previous step-in was made in a method that must not be stepped. We need to store this information in order to step in again as soon as we leave this method.
// Usually this is code related to step filtering, but in some cases we could also filter compiler-generated code and code covered by the StepThrough attribute.
bool &GetFilteredPrevStep()
{
    static bool filteredPrevStep{false};
    return filteredPrevStep;
}

} // unnamed namespace

HRESULT DisableAll(ICorDebugProcess *pProcess)
{
    HRESULT Status = S_OK;
    IfFailRet(SimpleStepper::DisableAll(pProcess));
    return AsyncStepper::DisableAll();
}

HRESULT DisableAll(ICorDebugAppDomain *pAppDomain)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pAppDomain->GetProcess(&trProcess));
    return DisableAll(trProcess);
}

HRESULT DisableAllSimpleSteppers(ICorDebugProcess *pProcess)
{
    return SimpleStepper::DisableAll(pProcess);
}

HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType)
{
    HRESULT Status = S_OK;
    GetFilteredPrevStep() = false;
    GetInitialStepType() = stepType;

    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pThread->GetProcess(&trProcess));
    DisableAll(trProcess);

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    IfFailRet(DebugInfo::GetSequencePointByFrame(trFrame, GetStepStartSP()));

    IfFailRet(AsyncStepper::SetupStep(pThread, stepType));
    if (Status == S_USE_SIMPLE_STEPPER)
    {
        return SimpleStepper::SetupStep(pThread, stepType);
    }

    return S_OK;
}

HRESULT ManagedCallbackBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    HRESULT Status = S_OK;
    // Check async stepping-related breakpoints first: the user can't set up breakpoints at await block yield or resume offsets manually,
    // so async stepping-related breakpoints are not part of any user breakpoint-related data (that will be checked in a separate thread, see code below).
    IfFailRet(AsyncStepper::ManagedCallbackBreakpoint(pThread));
    if (Status == S_IGNORE)
    {
        return S_IGNORE;
    }

    return SimpleStepper::ManagedCallbackBreakpoint(pAppDomain, pThread);
}

HRESULT ManagedCallbackStepComplete(ICorDebugThread *pThread, CorDebugStepReason reason)
{
    // From ECMA-335
    static const std::unordered_set<WSTRING> g_operatorMethodNames
    {
    // Cast operators
        W("op_Implicit"),
        W("op_Explicit"),
    // Unary operators
        W("op_Decrement"),                    // --
        W("op_Increment"),                    // ++
        W("op_UnaryNegation"),                // - (unary)
        W("op_UnaryPlus"),                    // + (unary)
        W("op_LogicalNot"),                   // !
        W("op_True"),                         // Not defined
        W("op_False"),                        // Not defined
        W("op_AddressOf"),                    // & (unary)
        W("op_OnesComplement"),               // ~
        W("op_PointerDereference"),           // * (unary)
    // Binary operators
        W("op_Addition"),                     // + (binary)
        W("op_Subtraction"),                  // - (binary)
        W("op_Multiply"),                     // * (binary)
        W("op_Division"),                     // /
        W("op_Modulus"),                      // %
        W("op_ExclusiveOr"),                  // ^
        W("op_BitwiseAnd"),                   // & (binary)
        W("op_BitwiseOr"),                    // |
        W("op_LogicalAnd"),                   // &&
        W("op_LogicalOr"),                    // ||
        W("op_Assign"),                       // Not defined (= is not the same)
        W("op_LeftShift"),                    // <<
        W("op_RightShift"),                   // >>
        W("op_SignedRightShift"),             // Not defined
        W("op_UnsignedRightShift"),           // Not defined
        W("op_Equality"),                     // ==
        W("op_GreaterThan"),                  // >
        W("op_LessThan"),                     // <
        W("op_Inequality"),                   // !=
        W("op_GreaterThanOrEqual"),           // >=
        W("op_LessThanOrEqual"),              // <=
        W("op_UnsignedRightShiftAssignment"), // Not defined
        W("op_MemberSelection"),              // ->
        W("op_RightShiftAssignment"),         // >>=
        W("op_MultiplicationAssignment"),     // *=
        W("op_PointerToMemberSelection"),     // ->*
        W("op_SubtractionAssignment"),        // -=
        W("op_ExclusiveOrAssignment"),        // ^=
        W("op_LeftShiftAssignment"),          // <<=
        W("op_ModulusAssignment"),            // %=
        W("op_AdditionAssignment"),           // +=
        W("op_BitwiseAndAssignment"),         // &=
        W("op_BitwiseOrAssignment"),          // |=
        W("op_Comma"),                        // ,
        W("op_DivisionAssignment")            // /=
    };

    HRESULT Status = S_OK;

    const StepType &initialStepType = GetInitialStepType();
    bool &filteredPrevStep = GetFilteredPrevStep();

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFrame->GetFunction(&trFunction));
    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    const auto methodShouldBeFiltered = [&]() -> bool
    {
        // In case stepping by method code lines or return to caller, don't check filtering (don't need to):
        // 1) filtering check for this method was already "passed" or 2) execution was stopped at breakpoint inside method or its callee.
        if (reason != CorDebugStepReason::STEP_CALL)
        {
            return false;
        }

        ULONG nameLen = 0;
        if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                              nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            return false;
        }

        WSTRING szFunctionName(nameLen, '\0');
        if (SUCCEEDED(trMDImport->GetMethodProps(methodDef, nullptr, szFunctionName.data(), nameLen, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            // Remove null terminator that was included in the length
            if (!szFunctionName.empty() && szFunctionName.back() == '\0')
            {
                szFunctionName.pop_back();
            }

            if (g_operatorMethodNames.find(szFunctionName) != g_operatorMethodNames.cend())
            {
                return true;
            }
        }

        mdProperty propertyDef = mdPropertyNil;
        ULONG numProperties = 0;
        HCORENUM propEnum = nullptr;
        while (SUCCEEDED(trMDImport->EnumProperties(&propEnum, typeDef, &propertyDef, 1, &numProperties)) && numProperties != 0)
        {
            mdMethodDef mdSetter = mdMethodDefNil;
            mdMethodDef mdGetter = mdMethodDefNil;
            if (SUCCEEDED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr,
                                                       nullptr, nullptr, nullptr, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
            {
                if (mdSetter != methodDef && mdGetter != methodDef)
                {
                    continue;
                }

                trMDImport->CloseEnum(propEnum);
                return true;
            }
        }
        trMDImport->CloseEnum(propEnum);

        return false;
    };

    // https://docs.microsoft.com/en-us/visualstudio/debugger/navigating-through-code-with-the-debugger?view=vs-2019#BKMK_Step_into_properties_and_operators_in_managed_code
    // The debugger steps over properties and operators in managed code by default. In most cases, this provides a better debugging experience.
    if (GetStepFiltering() && methodShouldBeFiltered())
    {
        IfFailRet(SimpleStepper::SetupStep(pThread, StepType::STEP_OUT));
        filteredPrevStep = true;
        return S_IGNORE;
    }

    const bool prevFilteredPrevStep = filteredPrevStep;
    filteredPrevStep = false;

    // Same behavior as MS vsdbg and MSVS C# debugger have - step only for code with PDB loaded (no matter JMC enabled or not by user).
    uint32_t ipOffset = 0;
    uint32_t ilNextUserCodeOffset = 0;
    if (SUCCEEDED(Status = DebugInfo::GetNextUserCodeILOffset(trFrame, ipOffset, ilNextUserCodeOffset)))
    {
        if (reason == CorDebugStepReason::STEP_NORMAL)
        {
            if (ipOffset != ilNextUserCodeOffset)
            {
                // Step completed on some compiler generated (non-user) code inside user code (for example, `finally` block related code)
                IfFailRet(SimpleStepper::SetupStep(pThread, initialStepType));
                return S_IGNORE;
            }
            else
            {
                // Step completed on same location in source as it was started, this happens when some user code block has several
                // SequencePoints for same line (for example, `using` related code could mix user/compiler generated code for same line).
                PDB::SequencePoint sp;
                IfFailRet(DebugInfo::GetSequencePointByFrame(trFrame, sp));
                const PDB::SequencePoint &stepStartSP = GetStepStartSP();
                if (sp.startLine == stepStartSP.startLine &&
                    sp.startColumn == stepStartSP.startColumn &&
                    sp.endLine == stepStartSP.endLine &&
                    sp.endColumn == stepStartSP.endColumn &&
                    sp.sourceFileIndex == stepStartSP.sourceFileIndex)
                {
                    IfFailRet(SimpleStepper::SetupStep(pThread, initialStepType));
                    return S_IGNORE;
                }
            }
        }
        // Current IL offset less than IL offset of next close user code line (for example, step-in into async method)
        else if (reason == CorDebugStepReason::STEP_CALL && ipOffset < ilNextUserCodeOffset)
        {
            IfFailRet(SimpleStepper::SetupStep(pThread, StepType::STEP_OVER));
            return S_IGNORE;
        }
        // returned from filtered method
        else if (reason == CorDebugStepReason::STEP_RETURN && prevFilteredPrevStep)
        {
            IfFailRet(SimpleStepper::SetupStep(pThread, StepType::STEP_IN));
            return S_IGNORE;
        }
    }
    else if (Status == CORDBG_E_CODE_NOT_AVAILABLE) // no user code available after ipOffset
    {
        IfFailRet(SimpleStepper::SetupStep(pThread, initialStepType));
        // In case step-in will return from method and no user code was called in user module, step-in again.
        filteredPrevStep = true;
        return S_IGNORE;
    }
    else // Note, in case JMC enabled step, ManagedCallbackStepComplete() called only for user module code.
    {
        return Status;
    }

    // Care about attributes for "JMC disabled" case.
    if (!Config::GetJustMyCode())
    {
        static const std::vector<WSTRING> attrNames{DebuggerAttribute::GetHidden(), DebuggerAttribute::GetStepThrough()};

        if (HasAttribute(trMDImport, typeDef, DebuggerAttribute::GetStepThrough()) ||
            HasAttribute(trMDImport, methodDef, attrNames))
        {
            IfFailRet(SimpleStepper::SetupStep(pThread, StepType::STEP_IN));
            // In case step-in will return from filtered method and no user code was called, step-in again.
            filteredPrevStep = true;

            return S_IGNORE;
        }
    }

    // Note, reset steppers right before return only.
    SimpleStepper::ManagedCallbackStepComplete();
    AsyncStepper::ManagedCallbackStepComplete();

    return S_OK;
}

void SetStepFiltering(bool enable)
{
    GetStepFiltering() = enable;
}

void Cleanup()
{
    // Don't reset the protocol-provided settings: StepFiltering.
    // Only the internal state related to process execution is reset here.

    GetInitialStepType() = StepType::STEP_OVER;
    GetStepStartSP() = PDB::SequencePoint{};
    GetFilteredPrevStep() = false;

    SimpleStepper::Cleanup();
    AsyncStepper::Cleanup();
}

} // namespace dncdbg::Steppers
