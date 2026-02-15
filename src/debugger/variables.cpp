// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/variables.h"
#include "debugger/evalhelpers.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/evaluator.h"
#include "debugger/valueprint.h"
#include "types/types.h"
#include "metadata/typeprinter.h"
#include <unordered_set>
#include <vector>

namespace dncdbg
{

namespace
{

void GetNumChild(Evaluator *pEvaluator, ICorDebugValue *pValue, int &numChild, bool static_members)
{
    numChild = 0;

    if (pValue == nullptr)
    {
        return;
    }

    int numStatic = 0;
    int numInstance = 0;
    // No thread and FrameLevel{0} here, since we need only count children.
    if (FAILED(pEvaluator->WalkMembers(pValue, nullptr, FrameLevel{0}, nullptr, false,
               [&](ICorDebugType *, bool is_static, const std::string &,
                   const Evaluator::GetValueCallback &, Evaluator::SetterData *)
                {
                    if (is_static)
                    {
                        numStatic++;
                    }
                    else
                    {
                        numInstance++;
                    }
                    return S_OK;
                })))
    {
        return;
    }

    if (static_members)
    {
        numChild = numStatic;
    }
    else
    {
        // Note, "+1", since all static members will be "packed" into "Static members" entry
        numChild = (numStatic > 0) ? numInstance + 1 : numInstance;
    }
}

struct VariableMember
{
    std::string name;
    std::string ownerType;
    ToRelease<ICorDebugValue> value;
    VariableMember(const std::string &name, std::string &ownerType, ICorDebugValue *pValue) // NOLINT(modernize-pass-by-value)
        : name(name),
          ownerType(std::move(ownerType)),
          value(pValue)
    {
    }

    VariableMember(VariableMember &&that) = default;
    VariableMember(const VariableMember &that) = delete;
    VariableMember &operator=(VariableMember &&) = delete;
    VariableMember &operator=(const VariableMember &) = delete;
    ~VariableMember() = default;
};

HRESULT FillValueAndType(VariableMember &member, Variable &var)
{
    if (member.value == nullptr)
    {
        // "SUCCEEDED" result, variable found but error during value receive itself.
        // For example, in case of eval flags `EVAL_NOFUNCEVAL` and property.
        var.value = "<error>";
        return S_FALSE;
    }

    TypePrinter::GetTypeOfValue(member.value, var.type);
    return PrintValue(member.value, var.value, true);
}

HRESULT FetchFieldsAndProperties(Evaluator *pEvaluator, ICorDebugValue *pInputValue, ICorDebugThread *pThread,
                                 FrameLevel frameLevel, std::vector<VariableMember> &members, bool fetchOnlyStatic,
                                 bool &hasStaticMembers, int childStart, int childEnd)
{
    hasStaticMembers = false;
    HRESULT Status = S_OK;

    DWORD threadId = 0;
    IfFailRet(pThread->GetID(&threadId));

    int currentIndex = -1;

    IfFailRet(pEvaluator->WalkMembers(pInputValue, pThread, frameLevel, nullptr, false,
        [&](ICorDebugType *pType, bool is_static, const std::string &name,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *)
        {
            if (is_static)
            {
                hasStaticMembers = true;
            }

            const bool addMember = fetchOnlyStatic ? is_static : !is_static;
            if (!addMember)
            {
                return S_OK;
            }

            ++currentIndex;
            if (currentIndex < childStart ||
                currentIndex >= childEnd)
            {
                return S_OK;
            }

            // Note, in this case error is not fatal, but if protocol side need cancel command execution, stop walk and return error to caller.
            ToRelease<ICorDebugValue> iCorResultValue;
            if (getValue(&iCorResultValue, false) == COR_E_OPERATIONCANCELED)
            {
                return COR_E_OPERATIONCANCELED;
            }

            std::string className;
            if (pType)
            {
                IfFailRet(TypePrinter::GetTypeOfValue(pType, className));
            }

            members.emplace_back(name, className, iCorResultValue.Detach());
            return S_OK;
        }));

    return S_OK;
}

void FixupInheritedFieldNames(std::vector<VariableMember> &members)
{
    std::unordered_set<std::string> names;
    for (auto &it : members)
    {
        auto r = names.insert(it.name);
        if (!r.second)
        {
            it.name += " (" + it.ownerType + ")";
        }
    }
}

} // unnamed namespace

// Caller should guarantee, that pProcess is not null.
HRESULT Variables::GetVariables(ICorDebugProcess *pProcess, uint32_t variablesReference, VariablesFilter filter,
                                int start, int count, std::vector<Variable> &variables)
{
    const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

    auto it = m_references.find(variablesReference);
    if (it == m_references.end())
    {
        return E_FAIL;
    }

    VariableReference &ref = it->second;

    HRESULT Status = S_OK;

    ToRelease<ICorDebugThread> pThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(ref.frameId.getThread()), &pThread));

    // Named and Indexed variables are in the same index (internally), Named variables go first
    if (filter == VariablesFilter::Named && (start + count > ref.namedVariables || count == 0))
    {
        count = ref.namedVariables - start;
    }
    if (filter == VariablesFilter::Indexed)
    {
        start += ref.namedVariables;
    }

    if (ref.IsScope())
    {
        IfFailRet(GetStackVariables(ref.frameId, pThread, start, count, variables));
    }
    else
    {
        IfFailRet(GetChildren(ref, pThread, start, count, variables));
    }
    return S_OK;
}

HRESULT Variables::AddVariableReference(Variable &variable, FrameId frameId, ICorDebugValue *pValue, ValueKind valueKind)
{
    const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

    if (m_references.size() == std::numeric_limits<uint32_t>::max())
    {
        return E_FAIL;
    }

    int numChild = 0;
    GetNumChild(m_sharedEvaluator.get(), pValue, numChild, valueKind == ValueKind::Class);
    if (numChild == 0)
    {
        return S_OK;
    }

    variable.namedVariables = numChild;
    variable.variablesReference = static_cast<uint32_t>(m_references.size()) + 1;
    pValue->AddRef();
    VariableReference variableReference(variable, frameId, pValue, valueKind);
    m_references.emplace(variable.variablesReference, std::move(variableReference));

    return S_OK;
}

HRESULT Variables::GetExceptionVariable(FrameId frameId, ICorDebugThread *pThread, Variable &var)
{
    ToRelease<ICorDebugValue> pExceptionValue;
    if (SUCCEEDED(pThread->GetCurrentException(&pExceptionValue)) && pExceptionValue != nullptr)
    {
        var.name = "$exception";
        var.evaluateName = var.name;

        HRESULT Status = S_OK;
        IfFailRet(PrintValue(pExceptionValue, var.value));
        IfFailRet(TypePrinter::GetTypeOfValue(pExceptionValue, var.type));

        return AddVariableReference(var, frameId, pExceptionValue, ValueKind::Variable);
    }

    return E_FAIL;
}

HRESULT Variables::GetStackVariables(FrameId frameId, ICorDebugThread *pThread, int start, int count,
                                     std::vector<Variable> &variables)
{
    HRESULT Status = S_OK;
    int currentIndex = -1;
    Variable var;
    if (SUCCEEDED(GetExceptionVariable(frameId, pThread, var)))
    {
        variables.push_back(var);
        ++currentIndex;
    }

    if (FAILED(Status = m_sharedEvaluator->WalkStackVars(pThread, frameId.getLevel(),
        [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
        {
            ++currentIndex; // NOLINT(bugprone-inc-dec-in-conditions)

            if (currentIndex < start)
            {
                return S_OK;
            }
            if (count != 0 && currentIndex >= start + count)
            {
                return E_ABORT; // Fast exit from cycle.
            }

            Variable var;
            var.name = name;
            var.evaluateName = var.name;
            ToRelease<ICorDebugValue> iCorValue;
            IfFailRet(getValue(&iCorValue, false));
            IfFailRet(TypePrinter::GetTypeOfValue(iCorValue, var.type));
            IfFailRet(PrintValue(iCorValue, var.value));

            IfFailRet(AddVariableReference(var, frameId, iCorValue, ValueKind::Variable));
            variables.push_back(var);
            return S_OK;
        })) &&
        Status != E_ABORT)
    {
        return Status;
    }

    return S_OK;
}

HRESULT Variables::GetScopes(ICorDebugProcess *pProcess, FrameId frameId, std::vector<Scope> &scopes)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> pThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &pThread));
    int namedVariables = 0;
    uint32_t variablesReference = 0;

    ToRelease<ICorDebugValue> pExceptionValue;
    if (SUCCEEDED(pThread->GetCurrentException(&pExceptionValue)) && pExceptionValue != nullptr)
    {
        namedVariables++;
    }

    IfFailRet(m_sharedEvaluator->WalkStackVars(pThread, frameId.getLevel(),
        [&](const std::string &/*name*/, const Evaluator::GetValueCallback &) -> HRESULT
        {
            namedVariables++;
            return S_OK;
        }));

    if (namedVariables > 0)
    {
        const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

        if (m_references.size() == std::numeric_limits<uint32_t>::max())
        {
            return E_FAIL;
        }

        variablesReference = static_cast<uint32_t>(m_references.size()) + 1;
        VariableReference scopeReference(variablesReference, frameId, namedVariables);
        m_references.emplace(variablesReference, std::move(scopeReference));
    }

    scopes.emplace_back(variablesReference, "Locals", namedVariables);

    return S_OK;
}

HRESULT Variables::GetChildren(VariableReference &ref, ICorDebugThread *pThread, int start, int count, std::vector<Variable> &variables)
{
    if (ref.IsScope())
    {
        return E_INVALIDARG;
    }

    if (ref.iCorValue == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    std::vector<VariableMember> members;
    bool hasStaticMembers = false;

    IfFailRet(FetchFieldsAndProperties(m_sharedEvaluator.get(), ref.iCorValue, pThread, ref.frameId.getLevel(),
                                       members, ref.valueKind == ValueKind::Class, hasStaticMembers, start,
                                       count == 0 ? INT_MAX : start + count));

    FixupInheritedFieldNames(members);

    for (auto &it : members)
    {
        Variable var;
        var.name = it.name;
        const bool isIndex = !it.name.empty() && it.name.at(0) == '[';
        if (var.name.find('(') == std::string::npos) // expression evaluator does not support typecasts
        {
            var.evaluateName = ref.evaluateName + (isIndex ? "" : ".") + var.name;
        }
        IfFailRet(FillValueAndType(it, var));
        IfFailRet(AddVariableReference(var, ref.frameId, it.value, ValueKind::Variable));
        variables.push_back(var);
    }

    if (ref.valueKind == ValueKind::Variable && hasStaticMembers)
    {
        const bool staticsInRange = start < ref.namedVariables && (count == 0 || start + count >= ref.namedVariables);
        if (staticsInRange)
        {
            ToRelease<ICorDebugValue2> pValue2;
            IfFailRet(ref.iCorValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2)));
            ToRelease<ICorDebugType> pType;
            IfFailRet(pValue2->GetExactType(&pType));
            // Note, this call could return S_FALSE without ICorDebugValue creation in case type don't have static members.
            IfFailRet(m_sharedEvalHelpers->CreatTypeObjectStaticConstructor(pThread, pType, nullptr, false));

            Variable var;
            var.name = "Static members";
            IfFailRet(TypePrinter::GetTypeOfValue(ref.iCorValue, var.evaluateName)); // do not expose type for this fake variable

            IfFailRet(AddVariableReference(var, ref.frameId, ref.iCorValue, ValueKind::Class));
            variables.push_back(var);
        }
    }

    return S_OK;
}

HRESULT Variables::Evaluate(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expression,
                            Variable &variable, std::string &output)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> pThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &pThread));

    ToRelease<ICorDebugValue> pResultValue;
    const FrameLevel frameLevel = frameId.getLevel();
    IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(pThread, frameLevel, expression,
                                                           &pResultValue, output));

    variable.evaluateName = expression;
    IfFailRet(TypePrinter::GetTypeOfValue(pResultValue, variable.type));
    IfFailRet(PrintValue(pResultValue, variable.value));

    return AddVariableReference(variable, frameId, pResultValue, ValueKind::Variable);
}

HRESULT Variables::SetVariable(ICorDebugProcess *pProcess, const std::string &name, const std::string &value,
                               uint32_t ref, std::string &output)
{
    const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

    auto it = m_references.find(ref);
    if (it == m_references.end())
    {
        return E_FAIL;
    }

    VariableReference &varRef = it->second;
    HRESULT Status = S_OK;

    ToRelease<ICorDebugThread> pThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(varRef.frameId.getThread()), &pThread));

    if (varRef.IsScope())
    {
        IfFailRet(SetStackVariable(varRef, pThread, name, value, output));
    }
    else
    {
        IfFailRet(SetChild(varRef, pThread, name, value, output));
    }

    return S_OK;
}

HRESULT Variables::SetStackVariable(VariableReference &ref, ICorDebugThread *pThread, const std::string &name,
                                    const std::string &value, std::string &output)
{
    HRESULT Status = S_OK;
    bool found = false;

    if (FAILED(Status = m_sharedEvaluator->WalkStackVars(pThread, ref.frameId.getLevel(),
        [&](const std::string &varName, const Evaluator::GetValueCallback &getValue) -> HRESULT
        {
            if (varName != name)
            {
                return S_OK;
            }

            ToRelease<ICorDebugValue> iCorValue;
            IfFailRet(getValue(&iCorValue, false));
            IfFailRet(m_sharedEvaluator->SetValue(pThread, ref.frameId.getLevel(), iCorValue, &getValue,
                                                  nullptr, value, output));
            IfFailRet(PrintValue(iCorValue, output));
            found = true;
            return E_ABORT; // Fast exit from cycle.
        })) &&
        Status != E_ABORT)
    {
        return Status;
    }

    if (!found)
    {
        output = "Variable name not found.";
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Variables::SetChild(VariableReference &ref, ICorDebugThread *pThread, const std::string &name,
                            const std::string &value, std::string &output)
{
    if (ref.IsScope())
    {
        return E_INVALIDARG;
    }

    if (ref.iCorValue == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    bool found = false;

    if (FAILED(Status = m_sharedEvaluator->WalkMembers(ref.iCorValue, pThread, ref.frameId.getLevel(), nullptr, true,
        [&](ICorDebugType *, bool /*is_static*/, const std::string &varName,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData) -> HRESULT
        {
            if (varName != name)
            {
                return S_OK;
            }

            if (setterData && !setterData->setterFunction)
            {
                return E_FAIL;
            }

            ToRelease<ICorDebugValue> iCorValue;
            IfFailRet(getValue(&iCorValue, false));
            IfFailRet(m_sharedEvaluator->SetValue(pThread, ref.frameId.getLevel(), iCorValue, &getValue,
                                                  setterData, value, output));
            IfFailRet(PrintValue(iCorValue, output));
            found = true;
            return E_ABORT; // Fast exit from cycle.
        })) &&
        Status != E_ABORT)
    {
        return Status;
    }

    if (!found)
    {
        output = "Variable name not found.";
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Variables::SetExpression(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expression,
                                 const std::string &value, std::string &output)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> pThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &pThread));

    ToRelease<ICorDebugValue> iCorValue;
    bool editable = false;
    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(pThread, frameId.getLevel(), expression,
                                                           &iCorValue, output, &editable, &setterData));
    if (!editable ||
        (editable && (setterData != nullptr) && (setterData->setterFunction == nullptr))) // property, that don't have setter
    {
        output = "'" + expression + "' cannot be assigned to";
        return E_INVALIDARG;
    }

    IfFailRet(m_sharedEvaluator->SetValue(pThread, frameId.getLevel(), iCorValue, nullptr, setterData.get(), value, output));
    IfFailRet(PrintValue(iCorValue, output));
    return S_OK;
}

} // namespace dncdbg
