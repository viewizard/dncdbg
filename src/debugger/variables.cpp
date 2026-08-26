// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/variables.h"
#include "debugger/evaluation/evalexec.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/valueprint.h"
#include "types/types.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include <array>
#include <unordered_set>
#include <vector>

namespace dncdbg
{

namespace
{

struct VariableMember
{
    std::string name;
    std::string ownerType;
    std::string realDisplayTypeName;
    ToRelease<ICorDebugValue> trValue;
    std::string customDisplayTextWithEval;
    uint32_t skipToChildIndex;
    bool extendEntry;

    VariableMember(std::string name_, std::string ownerType_, std::string realDisplayTypeName_, ICorDebugValue *pValue,
                   std::string customDisplayTextWithEval_, uint32_t skipToChildIndex_ = 0, bool extendEntry_ = false)
        : name(std::move(name_)),
          ownerType(std::move(ownerType_)),
          realDisplayTypeName(std::move(realDisplayTypeName_)),
          trValue(pValue),
          customDisplayTextWithEval(std::move(customDisplayTextWithEval_)),
          skipToChildIndex(skipToChildIndex_),
          extendEntry(extendEntry_)
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

    MetadataHelpers::GetFQDisplayTypeName(member.trValue, var.type);

    // Note, in case of `DebuggerDisplayAttribute` on field or property, build display text from attribute
    // and ignore `specifier`, since attribute text takes precedence over format specifiers (same as for type-level attribute).
    if (!member.customDisplayTextWithEval.empty())
    {
        std::vector<std::pair<std::string, bool>> textWithEvalParts;
        CreateTextWithEvalParts(member.customDisplayTextWithEval, textWithEvalParts);
        BuildTextWithEval(pEvaluator, pEvalStackMachine, pThread, member.trValue, textWithEvalParts, var.value);
        return S_OK;
    }

    return PrintValue(pThread, pEvaluator, pEvalStackMachine, member.trValue, specifier, var.value);
}

HRESULT FetchFieldsAndProperties(Evaluator *pEvaluator, ICorDebugThread *pThread, const Variables::VariableReference &ref,
                                 std::vector<VariableMember> &members, bool &hasStaticMembers)
{
    hasStaticMembers = false;
    HRESULT Status = S_OK;

    DWORD threadId = 0;
    IfFailRet(pThread->GetID(&threadId));

    uint32_t count = 0;
    static constexpr uint32_t maxCount = 25;

    IfFailRet(pEvaluator->WalkMembers(ref.trValue, pThread, ref.frameId.getLevel(), false, ref.specifier,
        [&](ICorDebugType *pType, bool isStatic, const std::string &name,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *, std::string *customDisplayTextWithEval) -> HRESULT
        {
            if (isStatic)
            {
                hasStaticMembers = true;
            }

            const bool addMember = ref.valueKind == ValueKind::Static ? isStatic : !isStatic;
            if (!addMember)
            {
                return S_OK; // Return success to continue walking.
            }

            count++;

            if (count <= ref.skipToChildIndex)
            {
                return S_OK; // Return success to continue walking.
            }

            if (count > ref.skipToChildIndex + maxCount)
            {
                ref.trValue->AddRef();
                members.emplace_back("[More]", std::string{}, std::string{}, ref.trValue, std::string{}, ref.skipToChildIndex + maxCount, true);
                return S_CAN_EXIT;
            }

            // Note, in this case error is not fatal, but if protocol side needs to
            // cancel command execution, stop walk and return error to caller.
            ToRelease<ICorDebugValue> trResultValue;
            std::string fallbackTypeName;
            if (getValue(&trResultValue, &fallbackTypeName) == COR_E_OPERATIONCANCELED)
            {
                return COR_E_OPERATIONCANCELED;
            }

            std::string displayTypeName;
            if (pType)
            {
                IfFailRet(MetadataHelpers::GetFQDisplayTypeName(pType, displayTypeName));
            }

            members.emplace_back(name, displayTypeName, fallbackTypeName, trResultValue.Detach(),
                                 customDisplayTextWithEval != nullptr ? *customDisplayTextWithEval : std::string{});
            return S_OK; // Return success to continue walking.
        }));

    return S_OK;
}

void FixupInheritedNames(std::vector<VariableMember> &members)
{
    std::unordered_set<std::string> usedNames;
    for (auto &it : members)
    {
        auto [iter, success] = usedNames.insert(it.name);

        if (!success && !it.ownerType.empty())
        {
            it.name += " (" + it.ownerType + ")";
        }
    }
}

HRESULT CreatePinnedHandle(ICorDebugValue *pValue, ICorDebugHandleValue **ppHandleValue)
{
    HRESULT Status = S_OK;

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(pValue->GetType(&elemType));

    // Note, reference objects (class, array, szarray, byref) may be moved or invalidated by the GC/runtime during
    // evaluations (which happen during break), so pin heap values via a GC handle that survives across continue-break
    // (same behavior as MS vsdbg).
    // DereferenceAndUnboxValue produces the inner heap object in trValue, so ICorDebugHeapValue2 must be queried on it
    // (not on pValue, which may be a byref/ELEMENT_TYPE_BYREF and does not support IID_ICorDebugHeapValue2).
    if (elemType == ELEMENT_TYPE_CLASS ||
        elemType == ELEMENT_TYPE_ARRAY ||
        elemType == ELEMENT_TYPE_SZARRAY ||
        elemType == ELEMENT_TYPE_BYREF)
    {
        BOOL isNull = FALSE;
        ToRelease<ICorDebugValue> trValue;
        IfFailRet(DereferenceAndUnboxValue(pValue, &trValue, &isNull));
        ToRelease<ICorDebugHeapValue2> trHeapValue2;
        if (isNull == FALSE && trValue != nullptr)
        {
            IfFailRet(trValue->QueryInterface(IID_ICorDebugHeapValue2, reinterpret_cast<void **>(&trHeapValue2)));
            IfFailRet(trHeapValue2->CreateHandle(CorDebugHandleType::HANDLE_PINNED, ppHandleValue));
            return S_OK;
        }
    }

    return E_FAIL;
}

} // unnamed namespace

// Caller should guarantee, that pProcess is not null.
HRESULT Variables::GetVariables(ICorDebugProcess *pProcess, uint32_t variablesReference, std::vector<Variable> &variables)
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

    if (ref.IsScope())
    {
        IfFailRet(GetStackVariables(ref.frameId, trThread, variables));
    }
    else
    {
        IfFailRet(GetChildren(ref, trThread, variables));
    }
    return S_OK;
}

HRESULT Variables::AddVariableReference(ICorDebugThread *pThread, Variable &variable, FrameId frameId, ICorDebugValue *pValue,
                                        ValueKind valueKind, FormatSpecifier specifier, uint32_t skipToChildIndex)
{
    const std::scoped_lock<std::recursive_mutex> lock(m_referencesMutex);

    if (m_references.size() == std::numeric_limits<uint32_t>::max())
    {
        return E_FAIL;
    }

    bool hasChildren = false;
    if (pValue != nullptr)
    {
        // Note: FrameLevel{0} is used here, since we only need to check whether the value has children.
        m_sharedEvaluator->WalkMembers(pValue, pThread, FrameLevel{0}, false, specifier,
            [&](ICorDebugType *, bool isStatic, const std::string &,
                const Evaluator::GetValueCallback &, Evaluator::SetterData *, std::string *) -> HRESULT
            {
                // Note, for ValueKind::Static (the "Static members" node) only static members are children.
                // For other kinds both static and instance members count, since static members are packed
                // into a separate "Static members" entry (see GetChildren()).
                if (!isStatic && valueKind == ValueKind::Static)
                {
                    return S_OK; // Return success to continue walking.
                }

                hasChildren = true;
                return S_CAN_EXIT;
            });
    }
    if (!hasChildren)
    {
        return S_OK;
    }

#ifdef BIT64
    assert(m_references.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
    variable.variablesReference = static_cast<uint32_t>(m_references.size()) + 1;

    ToRelease<ICorDebugHandleValue> trHandleValue;
    if (SUCCEEDED(CreatePinnedHandle(pValue, &trHandleValue)))
    {
        // Note, ICorDebugHandleValue derives from ICorDebugValue, so it can be stored in VariableReference.
        VariableReference variableReference(variable, frameId, trHandleValue.Detach(),
                                            valueKind, specifier, skipToChildIndex);
        m_references.emplace(variable.variablesReference, std::move(variableReference));
    }
    else
    {
        // Fallback for value types, null references or in case pinning failed - store raw pValue.
        pValue->AddRef();
        VariableReference variableReference(variable, frameId, pValue, valueKind, specifier, skipToChildIndex);
        m_references.emplace(variable.variablesReference, std::move(variableReference));
    }

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
        IfFailRet(MetadataHelpers::GetFQDisplayTypeName(trExceptionValue, var.type));

        return AddVariableReference(pThread, var, frameId, trExceptionValue, ValueKind::Variable, FormatSpecifier::None, 0);
    }

    return E_FAIL;
}

HRESULT Variables::GetStackVariables(FrameId frameId, ICorDebugThread *pThread, std::vector<Variable> &variables)
{
    Variable var;
    if (SUCCEEDED(GetExceptionVariable(frameId, pThread, var)))
    {
        variables.push_back(var);
    }

    return m_sharedEvaluator->WalkStackVars(pThread, frameId.getLevel(),
        [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
        {
            Variable var;
            var.name = name;
            var.evaluateName = var.name;
            ToRelease<ICorDebugValue> trValue;
            HRESULT Status = S_OK;
            std::string fallbackTypeName;
            // If we fail to parse one variable, don't skip parsing the remaining variables.
            if (FAILED(Status = getValue(&trValue, &fallbackTypeName)) ||
                FAILED(MetadataHelpers::GetFQDisplayTypeName(trValue, var.type)) ||
                FAILED(PrintValue(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trValue, FormatSpecifier::None, var.value)) ||
                FAILED(AddVariableReference(pThread, var, frameId, trValue, ValueKind::Variable, FormatSpecifier::None, 0)))
            {
                if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE)
                {
                    var.type = fallbackTypeName.empty() ? "unknown" : fallbackTypeName;
                    var.value = "Cannot obtain value of the local variable or argument because it is not available at this instruction pointer, possibly because it has been optimized away.";

                    if (FAILED(AddVariableReference(pThread, var, frameId, trValue, ValueKind::Variable, FormatSpecifier::None, 0)))
                    {
                        return S_OK; // Return success to continue walking.
                    }
                }
                else
                {
                    return S_OK; // Return success to continue walking.
                }
            }

            if (!fallbackTypeName.empty())
            {
                var.type = std::move(fallbackTypeName);
            }

            variables.push_back(var);
            return S_OK; // Return success to continue walking.
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
    bool haveVariables = false;
    uint32_t variablesReference = 0;

    ToRelease<ICorDebugValue> trExceptionValue;
    if (SUCCEEDED(trThread->GetCurrentException(&trExceptionValue)) && trExceptionValue != nullptr)
    {
        haveVariables = true;
    }

    if (!haveVariables)
    {
        IfFailRet(m_sharedEvaluator->WalkStackVars(trThread, frameId.getLevel(),
            [&](const std::string &/*name*/, const Evaluator::GetValueCallback &) -> HRESULT
            {
                haveVariables = true;
                return S_CAN_EXIT;
            }));
    }

    if (haveVariables)
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
        VariableReference scopeReference(variablesReference, frameId);
        m_references.emplace(variablesReference, std::move(scopeReference));
    }

    scopes.emplace_back(variablesReference, "Locals");

    return S_OK;
}

HRESULT Variables::GetChildren(const VariableReference &ref, ICorDebugThread *pThread, std::vector<Variable> &variables)
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

    IfFailRet(FetchFieldsAndProperties(m_sharedEvaluator.get(), pThread, ref, members, hasStaticMembers));

    FixupInheritedNames(members);

    for (auto &it : members)
    {
        Variable var;
        var.name = it.name;

        if (it.extendEntry)
        {
            var.evaluateName = ref.evaluateName;
#ifdef BIT64
            assert(m_references.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
            var.variablesReference = static_cast<uint32_t>(m_references.size()) + 1;

            ToRelease<ICorDebugHandleValue> trHandleValue;
            if (SUCCEEDED(CreatePinnedHandle(it.trValue, &trHandleValue)))
            {
                // Note, ICorDebugHandleValue derives from ICorDebugValue, so it can be stored in VariableReference.
                VariableReference variableReference(var, ref.frameId, trHandleValue.Detach(), ref.valueKind,
                                                    ref.specifier, it.skipToChildIndex);
                m_references.emplace(var.variablesReference, std::move(variableReference));
            }
            else
            {
                // Fallback for value types, null references or in case pinning failed - store raw it.trValue.
                VariableReference variableReference(var, ref.frameId, it.trValue.Detach(), ref.valueKind, ref.specifier, it.skipToChildIndex);
                m_references.emplace(var.variablesReference, std::move(variableReference));
            }
        }
        else
        {
            const bool isIndex = !it.name.empty() && it.name.at(0) == '[';
            if (var.name.find('(') == std::string::npos) // expression evaluator does not support typecasts
            {
                var.evaluateName = ref.evaluateName + (isIndex ? "" : ".") + var.name;
            }
            IfFailRet(FillValueAndType(pThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), ref.specifier, it, var));
            if (!it.realDisplayTypeName.empty())
            {
                var.type = std::move(it.realDisplayTypeName);
            }
            IfFailRet(AddVariableReference(pThread, var, ref.frameId, it.trValue, ValueKind::Variable, ref.specifier, it.skipToChildIndex));
        }

        variables.push_back(var);
    }

    if (ref.valueKind == ValueKind::Variable && hasStaticMembers && ref.skipToChildIndex == 0)
    {
        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(ref.trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));
        m_sharedEvalExec->CreateTypeObject(pThread, trType, nullptr);

        Variable var;
        var.name = "Static members";
        IfFailRet(MetadataHelpers::GetFQDisplayTypeName(ref.trValue, var.evaluateName)); // do not expose type for this fake variable
        IfFailRet(AddVariableReference(pThread, var, ref.frameId, ref.trValue, ValueKind::Static, ref.specifier, 0));
        variables.push_back(var);
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
    std::string realDisplayTypeName;
    IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(trThread, frameLevel, expression, specifier,
                                                           nullptr, &trResultValue, &realDisplayTypeName, output));

    variable.evaluateName = expression;
    if (realDisplayTypeName.empty())
    {
        IfFailRet(MetadataHelpers::GetFQDisplayTypeName(trResultValue, variable.type));
    }
    else
    {
        variable.type = realDisplayTypeName;
    }
    IfFailRet(PrintValue(trThread, m_sharedEvaluator.get(), m_sharedEvalStackMachine.get(), trResultValue, specifier, variable.value));

    return AddVariableReference(trThread, variable, frameId, trResultValue, ValueKind::Variable, specifier, 0);
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
                return S_OK; // Return success to continue walking.
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
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *setterData, std::string *) -> HRESULT
        {
            if (varName != name)
            {
                return S_OK; // Return success to continue walking.
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
                                                           nullptr, &trValue, nullptr, output, &editable, &setterData));
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
    std::string displayTypeName;
    MetadataHelpers::GetFQDisplayTypeName(trPrevValue, displayTypeName);
    if (displayTypeName.back() == '?') // System.Nullable<T>
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
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&elemType));

    if (elemType == ELEMENT_TYPE_STRING)
    {
        // FIXME: investigate why we can't use ICorDebugReferenceValue::SetValue() for a string in trValue in this case
        trValue.Free();
        IfFailRet(m_sharedEvalStackMachine->EvaluateExpression(pThread, frameLevel, value, FormatSpecifier::None,
                                                               nullptr, &trValue, nullptr, output));

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
