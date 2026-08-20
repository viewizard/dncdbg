// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_VARIABLES_H
#define DEBUGGER_VARIABLES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evaluator.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include <mutex>
#include <unordered_map>

namespace dncdbg
{

class EvalStackMachine;

class Variables
{
  public:

    Variables(std::shared_ptr<EvalExec> &sharedEvalExec,
              std::shared_ptr<Evaluator> &sharedEvaluator,
              std::shared_ptr<EvalStackMachine> &sharedEvalStackMachine)
        : m_sharedEvalExec(sharedEvalExec),
          m_sharedEvaluator(sharedEvaluator),
          m_sharedEvalStackMachine(sharedEvalStackMachine)
    {
    }

    HRESULT GetVariables(ICorDebugProcess *pProcess, uint32_t variablesReference, std::vector<Variable> &variables);

    HRESULT SetVariable(ICorDebugProcess *pProcess, const std::string &name, const std::string &value, uint32_t ref,
                        std::string &output);

    HRESULT SetExpression(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                          const std::string &value, std::string &output);

    HRESULT GetScopes(ICorDebugProcess *pProcess, FrameId frameId, std::vector<Scope> &scopes);

    HRESULT Evaluate(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                     Variable &variable, std::string &output);

    HRESULT GetExceptionVariable(FrameId frameId, ICorDebugThread *pThread, Variable &variable);

    void Cleanup()
    {
        m_referencesMutex.lock();
        m_references.clear();
        m_referencesMutex.unlock();
    }

  private:

    struct VariableReference
    {
        uint32_t variablesReference; // key

        std::string evaluateName;

        ValueKind valueKind;
        ToRelease<ICorDebugValue> trValue;
        FrameId frameId;
        FormatSpecifier specifier;

        VariableReference(const Variable &variable,
                          FrameId frameId,
                          ICorDebugValue *pValue,
                          ValueKind valueKind,
                          FormatSpecifier specifier)
            : variablesReference(variable.variablesReference),
              evaluateName(variable.evaluateName),
              valueKind(valueKind),
              trValue(pValue),
              frameId(frameId),
              specifier(specifier)
        {
        }

        VariableReference(uint32_t variablesReference,
                          FrameId frameId)
            : variablesReference(variablesReference),
              valueKind(ValueKind::Scope),
              trValue(nullptr),
              frameId(frameId),
              specifier(FormatSpecifier::None)
        {
        }

        [[nodiscard]] bool IsScope() const
        {
            return valueKind == ValueKind::Scope;
        }

        VariableReference(VariableReference &&) = default;
        VariableReference(const VariableReference &) = delete;
        VariableReference &operator=(VariableReference &&) = delete;
        VariableReference &operator=(const VariableReference &) = delete;
        ~VariableReference() = default;
    };

    std::shared_ptr<EvalExec> m_sharedEvalExec;
    std::shared_ptr<Evaluator> m_sharedEvaluator;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;

    std::recursive_mutex m_referencesMutex;
    std::unordered_map<uint32_t, VariableReference> m_references;

    HRESULT AddVariableReference(ICorDebugThread *pThread, Variable &variable, FrameId frameId,
                                 ICorDebugValue *pValue, ValueKind valueKind, FormatSpecifier specifier);

    HRESULT GetStackVariables(FrameId frameId, ICorDebugThread *pThread, std::vector<Variable> &variables);

    HRESULT GetChildren(const VariableReference &ref, ICorDebugThread *pThread, std::vector<Variable> &variables);

    HRESULT SetStackVariable(const VariableReference &ref, ICorDebugThread *pThread, const std::string &name,
                             const std::string &value, std::string &output);

    HRESULT SetChild(VariableReference &ref, ICorDebugThread *pThread, const std::string &name,
                     const std::string &value, std::string &output);

    HRESULT SetValue(ICorDebugThread *pThread, FrameLevel frameLevel, ToRelease<ICorDebugValue> &trPrevValue,
                     const Evaluator::GetValueCallback *getValue, Evaluator::SetterData *setterData,
                     const std::string &value, std::string &output);
};

} // namespace dncdbg

#endif // DEBUGGER_VARIABLES_H
