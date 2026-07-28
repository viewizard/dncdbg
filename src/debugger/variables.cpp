// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/variables.h"
#include "debugger/evaluation/evalexec.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/valueprint.h"
#include "types/types.h"
#include "metadata/typeprinter.h"
#include "utils/hresult.h"
#include <array>
#include <unordered_set>
#include <vector>

namespace dncdbg
{

namespace
{

void GetNumChild(ICorDebugThread *pThread, Evaluator *pEvaluator, ICorDebugValue *pValue,
                 FormatSpecifier specifier, bool static_members, int &numChild)
{
    numChild = 0;

    if (pValue == nullptr)
    {
        return;
    }

    int numStatic = 0;
    int numInstance = 0;
    // Note: FrameLevel{0} is used here, since we need only count children.
    if (FAILED(pEvaluator->WalkMembers(pValue, pThread, FrameLevel{0}, false, specifier,
               [&](ICorDebugType *, bool isStatic, const std::string &,
                   const Evaluator::GetValueCallback &, Evaluator::SetterData *) -> HRESULT
                {
                    if (isStatic)
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
    ToRelease<ICorDebugValue> trValue;
    VariableMember(std::string name, std::string &ownerType, ICorDebugValue *pValue)
        : name(std::move(name)),
          ownerType(std::move(ownerType)),
          trValue(pValue)
    {
    }

    VariableMember(VariableMember &&) = default;
    VariableMember(const VariableMember &) = delete;
    VariableMember &operator=(VariableMember &&) = delete;
    VariableMember &operator=(const VariableMember &) = delete;
    ~VariableMember() = default;
};

HRESULT FillValueAndType(ICorDebugThread *pThread, Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine,
                         FormatSpecifier specifier, VariableMember &member, Variable &var)
{
    if (member.trValue == nullptr)
    {
        // "SUCCEEDED" result, variable found but error during value receive itself.
        // For example, in case of eval flags `EVAL_NOFUNCEVAL` and property.
        var.value = "<error>";
        return S_FALSE;
    }

    TypePrinter::GetTypeOfValue(member.trValue, var.type);
    return PrintValue(pThread, pEvaluator, pEvalStackMachine, member.trValue, specifier, var.value);
}

HRESULT FetchFieldsAndProperties(Evaluator *pEvaluator, ICorDebugValue *pInputValue, ICorDebugThread *pThread,
                                 FrameLevel frameLevel, FormatSpecifier specifier, std::vector<VariableMember> &members,
                                 bool fetchOnlyStatic, bool &hasStaticMembers, int childStart, int childEnd)
{
    hasStaticMembers = false;
    HRESULT Status = S_OK;

    DWORD threadId = 0;
    IfFailRet(pThread->GetID(&threadId));

    int currentIndex = -1;

    IfFailRet(pEvaluator->WalkMembers(pInputValue, pThread, frameLevel, false, specifier,
        [&](ICorDebugType *pType, bool isStatic, const std::string &name,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *) -> HRESULT
        {
            if (isStatic)
            {
                hasStaticMembers = true;
            }

            const bool addMember = fetchOnlyStatic ? isStatic : !isStatic;
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
            ToRelease<ICorDebugValue> trResultValue;
            if (getValue(&trResultValue, nullptr) == COR_E_OPERATIONCANCELED)
            {
                return COR_E_OPERATIONCANCELED;
            }

            std::string className;
            if (pType)
            {
                IfFailRet(TypePrinter::GetTypeOfValue(pType, className));
            }

            members.emplace_back(name, className, trResultValue.Detach());
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

    const VariableReference &ref = it->second;

    HRESULT Status = S_OK;

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(ref.frameId.getThread()), &trThread));

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
        IfFailRet(GetStackVariables(ref.frameId, trThread, start, count, variables));
    }
    else
    {
        IfFailRet(GetChildren(ref, trThread, start, count, variables));
    }
    return S_OK;
}

HRESULT Variables::AddVariableReference(ICorDebugThread *pThread, Variable &variable, FrameId frameId,
                                        ICorDebugValue *pValue, ValueKind valueKind, FormatSpecifier specifier)
{
    const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

    if (m_references.size() == std::numeric_limits<uint32_t>::max())
    {
        return E_FAIL;
    }

    int numChild = 0;
    GetNumChild(pThread, m_sharedEvaluator.get(), pValue, specifier, valueKind == ValueKind::Class, numChild);
    if (numChild == 0)
    {
        return S_OK;
    }

#ifdef BIT64
    assert(m_references.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
    variable.namedVariables = numChild;
    variable.variablesReference = static_cast<uint32_t>(m_references.size()) + 1;
    pValue->AddRef();
    VariableReference variableReference(variable, frameId, pValue, valueKind, specifier);
    m_references.emplace(variable.variablesReference, std::move(variableReference));

    return S_OK;
}

HRESULT Variables::GetExceptionVariable(FrameId frameId, ICorDebugThread *pThread, Variable &var)
{
    ToRelease<ICorDebugValue> trExceptionValue;
    if (SUCCEEDED(pThread->GetCurrentException(&trExceptionValue)) && trExceptionValue != nullptr)
    {
        var.name = "$exception";
        var.evaluateName = var.name;

        HRESULT Status = S_OK;
        IfFailRet(PrintValue(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trExceptionValue, FormatSpecifier::None, var.value));
        IfFailRet(TypePrinter::GetTypeOfValue(trExceptionValue, var.type));

        return AddVariableReference(pThread, var, frameId, trExceptionValue, ValueKind::Variable, FormatSpecifier::None);
    }

    return E_FAIL;
}

HRESULT Variables::GetStackVariables(FrameId frameId, ICorDebugThread *pThread, int start, int count,
                                     std::vector<Variable> &variables)
{
    int currentIndex = -1;
    Variable var;
    if (SUCCEEDED(GetExceptionVariable(frameId, pThread, var)))
    {
        variables.push_back(var);
        ++currentIndex;
    }

    return m_sharedEvaluator->WalkStackVars(pThread, frameId.getLevel(),
        [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
        {
            ++currentIndex;

            if (currentIndex < start)
            {
                return S_OK;
            }
            if (count != 0 && currentIndex >= start + count)
            {
                return S_CAN_EXIT; // Fast exit from the loop.
            }

            Variable var;
            var.name = name;
            var.evaluateName = var.name;
            ToRelease<ICorDebugValue> trValue;
            HRESULT Status = S_OK;
            std::string fallbackTypeName;
            // If we fail to parse one variable, don't skip parsing the remaining variables.
            if (FAILED(Status = getValue(&trValue, &fallbackTypeName)) ||
                FAILED(TypePrinter::GetTypeOfValue(trValue, var.type)) ||
                FAILED(PrintValue(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trValue, FormatSpecifier::None, var.value)) ||
                FAILED(AddVariableReference(pThread, var, frameId, trValue, ValueKind::Variable, FormatSpecifier::None)))
            {
                if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE)
                {
                    var.type = fallbackTypeName.empty() ? "unknown" : fallbackTypeName;
                    var.value = "Cannot obtain value of the local variable or argument because it is not available at this instruction pointer, possibly because it has been optimized away.";

                    if (FAILED(AddVariableReference(pThread, var, frameId, trValue, ValueKind::Variable, FormatSpecifier::None)))
                    {
                        return S_OK;
                    }
                }
                else
                {
                    return S_OK;
                }
            }

            variables.push_back(var);
            return S_OK;
        });
}

HRESULT Variables::GetScopes(ICorDebugProcess *pProcess, FrameId frameId, std::vector<Scope> &scopes)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> trThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &trThread));
    int namedVariables = 0;
    uint32_t variablesReference = 0;

    ToRelease<ICorDebugValue> trExceptionValue;
    if (SUCCEEDED(trThread->GetCurrentException(&trExceptionValue)) && trExceptionValue != nullptr)
    {
        namedVariables++;
    }

    IfFailRet(m_sharedEvaluator->WalkStackVars(trThread, frameId.getLevel(),
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

#ifdef BIT64
        assert(m_references.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
        variablesReference = static_cast<uint32_t>(m_references.size()) + 1;
        VariableReference scopeReference(variablesReference, frameId, namedVariables);
        m_references.emplace(variablesReference, std::move(scopeReference));
    }

    scopes.emplace_back(variablesReference, "Locals", namedVariables);

    return S_OK;
}

HRESULT Variables::GetChildren(const VariableReference &ref, ICorDebugThread *pThread,
                               int start, int count, std::vector<Variable> &variables)
{
    if (ref.IsScope())
    {
        return E_INVALIDARG;
    }

    if (ref.trValue == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    std::vector<VariableMember> members;
    bool hasStaticMembers = false;

    IfFailRet(FetchFieldsAndProperties(m_sharedEvaluator.get(), ref.trValue, pThread, ref.frameId.getLevel(),
                                       ref.specifier, members, ref.valueKind == ValueKind::Class, hasStaticMembers,
                                       start, count == 0 ? INT_MAX : start + count));

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
        IfFailRet(FillValueAndType(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), ref.specifier, it, var));
        IfFailRet(AddVariableReference(pThread, var, ref.frameId, it.trValue, ValueKind::Variable, ref.specifier));
        variables.push_back(var);
    }

    if (ref.valueKind == ValueKind::Variable && hasStaticMembers)
    {
        const bool staticsInRange = start < ref.namedVariables && (count == 0 || start + count >= ref.namedVariables);
        if (staticsInRange)
        {
            ToRelease<ICorDebugValue2> trValue2;
            IfFailRet(ref.trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
            ToRelease<ICorDebugType> trType;
            IfFailRet(trValue2->GetExactType(&trType));
            m_sharedEvalExec->CreateTypeObject(pThread, trType, nullptr, false);

            Variable var;
            var.name = "Static members";
            IfFailRet(TypePrinter::GetTypeOfValue(ref.trValue, var.evaluateName)); // do not expose type for this fake variable

            IfFailRet(AddVariableReference(pThread, var, ref.frameId, ref.trValue, ValueKind::Class, ref.specifier));
            variables.push_back(var);
        }
    }

    return S_OK;
}

HRESULT Variables::Evaluate(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                            Variable &variable, std::string &output)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> trThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &trThread));

    FormatSpecifier specifier = FormatSpecifier::None;
    std::string expression;
    ParseFormatSpecifier(expressionWithFormat, expression, specifier);

    ToRelease<ICorDebugValue> trResultValue;
    const FrameLevel frameLevel = frameId.getLevel();
    IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(trThread, frameLevel, expression, specifier,
                                                           nullptr, &trResultValue, output));

    variable.evaluateName = expression;
    IfFailRet(TypePrinter::GetTypeOfValue(trResultValue, variable.type));
    IfFailRet(PrintValue(trThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trResultValue, specifier, variable.value));

    return AddVariableReference(trThread, variable, frameId, trResultValue, ValueKind::Variable, specifier);
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

    ToRelease<ICorDebugThread> trThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(varRef.frameId.getThread()), &trThread));

    if (varRef.IsScope())
    {
        IfFailRet(SetStackVariable(varRef, trThread, name, value, output));
    }
    else
    {
        IfFailRet(SetChild(varRef, trThread, name, value, output));
    }

    return S_OK;
}

HRESULT Variables::SetStackVariable(const VariableReference &ref, ICorDebugThread *pThread, const std::string &name,
                                    const std::string &value, std::string &output)
{
    HRESULT Status = S_OK;
    IfFailRet(m_sharedEvaluator->WalkStackVars(pThread, ref.frameId.getLevel(),
        [&](const std::string &varName, const Evaluator::GetValueCallback &getValue) -> HRESULT
        {
            if (varName != name)
            {
                return S_OK;
            }

            ToRelease<ICorDebugValue> trValue;
            IfFailRet(getValue(&trValue, nullptr));
            IfFailRet(SetValue(pThread, ref.frameId.getLevel(), trValue, &getValue, nullptr, value, output));
            IfFailRet(PrintValue(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trValue, FormatSpecifier::None, output));
            return S_CAN_EXIT; // Fast exit from the loop.
        }));

    if (output.empty())
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

    if (ref.trValue == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    IfFailRet(m_sharedEvaluator->WalkMembers(ref.trValue, pThread, ref.frameId.getLevel(), true, ref.specifier,
        [&](ICorDebugType *, bool /*isStatic*/, const std::string &varName,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData) -> HRESULT
        {
            if (varName != name)
            {
                return S_OK;
            }

            if (setterData && !setterData->trSetterFunction)
            {
                return E_FAIL;
            }

            ToRelease<ICorDebugValue> trValue;
            IfFailRet(getValue(&trValue, nullptr));
            IfFailRet(SetValue(pThread, ref.frameId.getLevel(), trValue, &getValue, setterData, value, output));
            IfFailRet(PrintValue(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trValue, ref.specifier, output));
            return S_CAN_EXIT; // Fast exit from the loop.
        }));

    if (output.empty())
    {
        output = "Variable name not found.";
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Variables::SetExpression(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                                 const std::string &value, std::string &output)
{
    const ThreadId threadId = frameId.getThread();
    if (!threadId)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugThread> trThread;
    IfFailRet(pProcess->GetThread(static_cast<int>(threadId), &trThread));

    FormatSpecifier specifier = FormatSpecifier::None;
    std::string expression;
    ParseFormatSpecifier(expressionWithFormat, expression, specifier);

    ToRelease<ICorDebugValue> trValue;
    bool editable = false;
    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(trThread, frameId.getLevel(), expression, specifier,
                                                           nullptr, &trValue, output, &editable, &setterData));
    if (!editable ||
        (setterData != nullptr && setterData->trSetterFunction == nullptr)) // property that doesn't have a setter
    {
        output = "'" + expression + "' cannot be assigned to";
        return E_INVALIDARG;
    }

    IfFailRet(SetValue(trThread, frameId.getLevel(), trValue, nullptr, setterData.get(), value, output));
    IfFailRet(PrintValue(trThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trValue, specifier, output));
    return S_OK;
}

HRESULT Variables::SetValue(ICorDebugThread *pThread, FrameLevel frameLevel, ToRelease<ICorDebugValue> &trPrevValue,
                            const Evaluator::GetValueCallback *getValue, Evaluator::SetterData *setterData,
                            const std::string &value, std::string &output)
{
    if (pThread == nullptr)
    {
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    std::string className;
    TypePrinter::GetTypeOfValue(trPrevValue, className);
    if (className.back() == '?') // System.Nullable<T>
    {
        ToRelease<ICorDebugValue> trValueValue;
        ToRelease<ICorDebugValue> trHasValueValue;
        IfFailRet(GetNullableValue(trPrevValue, &trValueValue, &trHasValueValue));

        if (value == "null")
        {
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trHasValueValue, "false", output));
        }
        else
        {
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trValueValue, value, output));
            IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trHasValueValue, "true", output));
        }
        if (getValue != nullptr)
        {
            trPrevValue.Free();
            IfFailRet((*getValue)(&trPrevValue, nullptr));
        }
        return S_OK;
    }

    // In case this is not a property, just change the value itself.
    if (setterData == nullptr)
    {
        return m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trPrevValue, value, output);
    }

    trPrevValue->AddRef();
    ToRelease<ICorDebugValue> trValue(trPrevValue.GetPtr());
    CorElementType corType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&corType));

    if (corType == ELEMENT_TYPE_STRING)
    {
        // FIXME: investigate why we can't use ICorDebugReferenceValue::SetValue() for string in trValue in this case
        trValue.Free();
        IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(pThread, frameLevel, value, FormatSpecifier::None, nullptr, &trValue, output));

        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(trValue->GetType(&elemType));
        if (elemType != ELEMENT_TYPE_STRING)
        {
            return E_INVALIDARG;
        }
    }
    else // Allow the stack machine to decide what types are supported.
    {
        IfFailRet(m_sharedEvalStackMachine->SetValueByExpression(pThread, frameLevel, trValue.GetPtr(), value, output));
    }

    // Call setter.
    if (setterData->trThisValue == nullptr)
    {
        return m_sharedEvalExec->CallFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetPtr(),
                                              nullptr, trValue.GetRef(), 1, FormatSpecifier::None, nullptr);
    }
    else
    {
        std::array<ICorDebugValue *, 2> argsValue{setterData->trThisValue, trValue};
        return m_sharedEvalExec->CallFunction(pThread, setterData->trSetterFunction, setterData->trPropertyType.GetPtr(),
                                              nullptr, argsValue.data(), 2, FormatSpecifier::None, nullptr);
    }
}

} // namespace dncdbg
