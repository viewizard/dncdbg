// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalstackmachine.h"
#include "debugger/evalhelpers.h"
#include "debugger/evalutils.h"
#include "debugger/evalwaiter.h"
#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/valueprint.h"
#include "expressionparser/helpers.h"
#include "expressionparser/parser.h"
#include "metadata/typeprinter.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iterator>
#include <sstream>
#include <vector>
#include <utility>

namespace dncdbg
{

namespace
{

void ReplaceAllSubstring(std::string &str, const std::string &from, const std::string &to)
{
    size_t start = 0;
    while (true)
    {
        start = str.find(from, start);
        if (start == std::string::npos)
        {
            break;
        }

        str.replace(start, from.length(), to);
        start += from.length();
    }
}

void ReplaceInternalNames(std::string &expression, bool restore = false)
{
    // TODO more internal names should be added (see internal variables supported by MSVS C# debugger)
    static const std::vector<std::pair<std::string, std::string>> internalNamesMap{
        {"$exception", "__INTERNAL_DNCDBG_EXCEPTION_VARIABLE"},
        {"$pid", "__INTERNAL_DNCDBG_PID_VARIABLE"},
        {"$tid", "__INTERNAL_DNCDBG_TID_VARIABLE"}
    };

    for (const auto &[internalVariable, tmpReplacement] : internalNamesMap)
    {
        if (restore)
        {
            ReplaceAllSubstring(expression, tmpReplacement, internalVariable);
        }
        else
        {
            ReplaceAllSubstring(expression, internalVariable, tmpReplacement);
        }
    }
}

HRESULT CreateBooleanValue(ICorDebugThread *pThread, ICorDebugValue **ppValue, bool setToTrue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugEval> trEval;
    IfFailRet(pThread->CreateEval(&trEval));
    IfFailRet(trEval->CreateValue(ELEMENT_TYPE_BOOLEAN, nullptr, ppValue));

    if (!setToTrue)
    {
        return S_OK;
    }

#ifdef DEBUG_INTERNAL_TESTS
    uint32_t cbSize = 0;
    IfFailRet((*ppValue)->GetSize(&cbSize));
    assert(cbSize == 1);
#endif // DEBUG_INTERNAL_TESTS
    uint8_t boolValue = 0;

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet((*ppValue)->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));
    boolValue = 1; // TRUE
    return trGenValue->SetValue(static_cast<void *>(&boolValue));
}

HRESULT CreateNullValue(ICorDebugThread *pThread, ICorDebugValue **ppValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugEval> trEval;
    IfFailRet(pThread->CreateEval(&trEval));
    // ICorDebugEval::CreateValue
    // If the value of elementType is ELEMENT_TYPE_CLASS, you get an "ICorDebugReferenceValue" (returned in ppValue)
    // representing the null object reference. You can use this object to pass null to a function evaluation that has
    // object reference parameters. You cannot set the ICorDebugValue to anything; it always remains null.
    return trEval->CreateValue(ELEMENT_TYPE_CLASS, nullptr, ppValue);
}

HRESULT CreateValueType(EvalWaiter *pEvalWaiter, ICorDebugThread *pThread, ICorDebugClass *pValueTypeClass,
                        ICorDebugValue **ppValue, void *ptr)
{
    HRESULT Status = S_OK;
    // Create value (without calling a constructor)
    IfFailRet(pEvalWaiter->WaitEvalResult(pThread, ppValue,
        [&](ICorDebugEval *pEval) -> HRESULT
        {
            // Note, this code execution is protected by EvalWaiter mutex.
            ToRelease<ICorDebugEval2> trEval2;
            IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2)));
            IfFailRet(trEval2->NewParameterizedObjectNoConstructor(pValueTypeClass, 0, nullptr));
            return S_OK;
        }));

    if (ptr == nullptr)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue> trEditableValue;
    IfFailRet(DereferenceAndUnboxValue(*ppValue, &trEditableValue, nullptr));

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trEditableValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    return trGenericValue->SetValue(ptr);
}

HRESULT GetFrontStackEntryValue(ICorDebugValue **ppResultValue,
                                std::unique_ptr<Evaluator::SetterData> *resultSetterData,
                                std::list<EvalStackEntry> &evalStack, EvalData &ed, std::string &output)
{
    HRESULT Status = S_OK;
    Evaluator::SetterData *inputPropertyData = nullptr;
    if (evalStack.front().editable)
    {
        inputPropertyData = evalStack.front().setterData.get();
    }
    else
    {
        resultSetterData = nullptr;
    }

    if (FAILED(Status = ed.pEvaluator->ResolveIdentifiers(ed.pThread, ed.frameLevel, evalStack.front().trValue,
                                                          inputPropertyData, evalStack.front().identifiers,
                                                          ppResultValue, resultSetterData, nullptr)) &&
        !evalStack.front().identifiers.empty())
    {
        std::ostringstream ss;
        for (size_t i = 0; i < evalStack.front().identifiers.size(); i++)
        {
            if (i != 0)
            {
                ss << ".";
            }
            ss << evalStack.front().identifiers.at(i);
        }
        output = "error: The name '" + ss.str() + "' does not exist in the current context";
    }

    return Status;
}

HRESULT GetFrontStackEntryType(ICorDebugType **ppResultType, std::list<EvalStackEntry> &evalStack, EvalData &ed,
                               std::string &output)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    if ((FAILED(Status = ed.pEvaluator->ResolveIdentifiers(ed.pThread, ed.frameLevel, evalStack.front().trValue, nullptr,
                                                           evalStack.front().identifiers, &trValue, nullptr, ppResultType)) &&
         !evalStack.front().identifiers.empty()) ||
        (trValue != nullptr))
    {
        std::ostringstream ss;
        for (size_t i = 0; i < evalStack.front().identifiers.size(); i++)
        {
            if (i != 0)
            {
                ss << ".";
            }
            ss << evalStack.front().identifiers.at(i);
        }
        if (trValue == nullptr)
        {
            output = "error: The type or namespace name '" + ss.str() + "' couldn't be found";
        }
        else
        {
            output = "error: '" + ss.str() + "' is a variable but is used like a type";
        }
        if (SUCCEEDED(Status))
        {
            Status = E_FAIL;
        }
    }

    return Status;
}

HRESULT GetArgData(ICorDebugValue *pTypeValue, std::string &typeName, CorElementType &elemType)
{
    HRESULT Status = S_OK;
    IfFailRet(pTypeValue->GetType(&elemType));
    if (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE)
    {
        ToRelease<ICorDebugValue2> trTypeValue2;
        IfFailRet(pTypeValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trTypeValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trTypeValue2->GetExactType(&trType));
        IfFailRet(TypePrinter::NameForTypeByType(trType, typeName));
    }
    return S_OK;
};

HRESULT CallUnaryOperator(const std::string &opName, ICorDebugValue *pValue, ICorDebugValue **pResultValue, EvalData &ed)
{
    HRESULT Status = S_OK;
    std::string typeName;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pValue, typeName, elemType));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Evaluator::WalkMethods(pValue, true,
        [&](bool isStatic, const std::string &methodName, Evaluator::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (!isStatic || methodArgs.size() != 1 || opName != methodName ||
                elemType != methodArgs.at(0).corType || typeName != methodArgs.at(0).typeName)
            {
                return S_OK; // Return with success to continue walk.
            }

            IfFailRet(getFunction(&trFunc));

            return S_CAN_EXIT; // Fast exit from loop.
        }));

    if (trFunc == nullptr)
    {
        return E_FAIL;
    }

    return ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, nullptr, nullptr, &pValue, 1, pResultValue);
}

HRESULT CallCastOperator(const std::string &opName, ICorDebugValue *pValue, CorElementType elemRetType,
                         const std::string &typeRetName, ICorDebugValue *pTypeValue, ICorDebugValue **pResultValue, EvalData &ed)
{
    HRESULT Status = S_OK;
    std::string typeName;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pTypeValue, typeName, elemType));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Evaluator::WalkMethods(pValue, true,
        [&](bool isStatic, const std::string &methodName, Evaluator::ReturnElementType &methodRet,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (!isStatic || methodArgs.size() != 1 || opName != methodName ||
                elemRetType != methodRet.corType || typeRetName != methodRet.typeName ||
                elemType != methodArgs.at(0).corType || typeName != methodArgs.at(0).typeName)
            {
                return S_OK; // Return with success to continue walk.
            }

            IfFailRet(getFunction(&trFunc));

            return S_CAN_EXIT; // Fast exit from loop.
        }));

    if (trFunc == nullptr)
    {
        return E_FAIL;
    }

    return ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, nullptr, nullptr, &pTypeValue, 1, pResultValue);
}

HRESULT CallCastOperator(const std::string &opName, ICorDebugValue *pValue, ICorDebugValue *pTypeRetValue,
                         ICorDebugValue *pTypeValue, ICorDebugValue **pResultValue, EvalData &ed)
{
    HRESULT Status = S_OK;
    std::string typeRetName;
    CorElementType elemRetType = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pTypeRetValue, typeRetName, elemRetType));

    return CallCastOperator(opName, pValue, elemRetType, typeRetName, pTypeValue, pResultValue, ed);
}

HRESULT GetRealValueWithType(ICorDebugValue *pValue, ICorDebugValue **ppResultValue, CorElementType *pElemType = nullptr)
{
    HRESULT Status = S_OK;
    // Dereference and unbox value, since we need real value.
    ToRelease<ICorDebugValue> trRealValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &trRealValue));
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(trRealValue->GetType(&elemType));
    // Note, in case of class (string is class), we must use reference instead.
    if (elemType == ELEMENT_TYPE_STRING || elemType == ELEMENT_TYPE_CLASS)
    {
        pValue->AddRef();
        *ppResultValue = pValue;
        if (pElemType != nullptr)
        {
            *pElemType = elemType;
        }
    }
    else
    {
        *ppResultValue = trRealValue.Detach();
        if (pElemType != nullptr)
        {
            *pElemType = elemType;
        }
    }

    return S_OK;
}

HRESULT CopyValue(ICorDebugValue *pSrcValue, ICorDebugValue *pDstValue, CorElementType elemTypeSrc, CorElementType elemTypeDst)
{
    if (elemTypeSrc != elemTypeDst)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    // Change address.
    if (elemTypeDst == ELEMENT_TYPE_STRING || elemTypeDst == ELEMENT_TYPE_CLASS)
    {
        ToRelease<ICorDebugReferenceValue> trRefNew;
        IfFailRet(pSrcValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trRefNew)));
        ToRelease<ICorDebugReferenceValue> trRefOld;
        IfFailRet(pDstValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trRefOld)));

        CORDB_ADDRESS addr = 0;
        IfFailRet(trRefNew->GetValue(&addr));
        return trRefOld->SetValue(addr);
    }

    // Copy data.
    if (PrimitiveTypes::IsPrimitiveType(elemTypeDst) ||
        elemTypeDst == ELEMENT_TYPE_VALUETYPE)
    {
        uint32_t cbSize = 0;
        IfFailRet(pSrcValue->GetSize(&cbSize));
        std::vector<uint8_t> elemValue(cbSize, 0);

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(pSrcValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->GetValue(static_cast<void *>(elemValue.data())));

        trGenericValue.Free();
        IfFailRet(pDstValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        return trGenericValue->SetValue(elemValue.data());
    }

    return E_NOTIMPL;
}

HRESULT ImplicitCast(ICorDebugValue *pSrcValue, ICorDebugValue *pDstValue, bool srcLiteral, EvalData &ed)
{
    HRESULT Status = S_OK;

    // Value with type was provided by caller, result must be implicitly cast to this type.
    // https://docs.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/conversions#implicit-numeric-conversions
    // https://docs.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/integral-numeric-types#integer-literals

    ToRelease<ICorDebugValue> trRealValue1;
    CorElementType elemType1 = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(pSrcValue, &trRealValue1, &elemType1));

    ToRelease<ICorDebugValue> trRealValue2;
    CorElementType elemType2 = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(pDstValue, &trRealValue2, &elemType2));

    bool haveSameType = true;
    if (elemType1 == elemType2)
    {
        if (elemType2 == ELEMENT_TYPE_VALUETYPE || elemType2 == ELEMENT_TYPE_CLASS)
        {
            std::string mdName1;
            IfFailRet(TypePrinter::NameForTypeByValue(trRealValue1, mdName1));
            std::string mdName2;
            IfFailRet(TypePrinter::NameForTypeByValue(trRealValue2, mdName2));

            if (mdName1 != mdName2)
            {
                haveSameType = false;
            }
        }
    }
    else
    {
        haveSameType = false;
    }

    if (!haveSameType && (elemType1 == ELEMENT_TYPE_VALUETYPE || elemType2 == ELEMENT_TYPE_VALUETYPE ||
                          elemType1 == ELEMENT_TYPE_CLASS || elemType2 == ELEMENT_TYPE_CLASS ||
                          elemType1 == ELEMENT_TYPE_STRING || elemType2 == ELEMENT_TYPE_STRING))
    {
        ToRelease<ICorDebugValue> trResultValue;
        if (FAILED(Status = CallCastOperator("op_Implicit", trRealValue1, trRealValue2, trRealValue1, &trResultValue, ed)) &&
            FAILED(Status = CallCastOperator("op_Implicit", trRealValue2, trRealValue2, trRealValue1, &trResultValue, ed)))
        {
            return Status;
        }

        trRealValue1.Free();
        IfFailRet(GetRealValueWithType(trResultValue, &trRealValue1, &elemType1));

        haveSameType = true;
    }

    if (haveSameType)
    {
        return CopyValue(trRealValue1, trRealValue2, elemType1, elemType2);
    }

    if (!PrimitiveTypes::IsPrimitiveType(elemType1) || !PrimitiveTypes::IsPrimitiveType(elemType2))
    {
        return E_INVALIDARG;
    }

    return (srcLiteral && elemType1 == ELEMENT_TYPE_I4) ?
        PrimitiveTypes::ImplicitCastIntLiteral(trRealValue1, trRealValue2) :
        PrimitiveTypes::ImplicitCast(trRealValue1, trRealValue2);
}

HRESULT CallBinaryOperator(const std::string &opName, ICorDebugValue *pValue, ICorDebugValue *pType1Value,
                           ICorDebugValue *pType2Value, ICorDebugValue **pResultValue, EvalData &ed)
{
    HRESULT Status = S_OK;
    std::string typeName1;
    CorElementType elemType1 = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pType1Value, typeName1, elemType1));
    std::string typeName2;
    CorElementType elemType2 = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pType2Value, typeName2, elemType2));
    // https://docs.microsoft.com/en-us/dotnet/csharp/language-reference/operators/operator-overloading
    // A unary operator has one input parameter. A binary operator has two input parameters. In each case,
    // at least one parameter must have type T or T? where T is the type that contains the operator declaration.
    std::string typeName;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pValue, typeName, elemType));
    if ((elemType != elemType1 || typeName != typeName1) && (elemType != elemType2 || typeName != typeName2))
    {
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugValue> trTypeValue;
    auto CallOperator =
        [&](std::function<HRESULT(std::vector<SigElementType> &)> cb) -> HRESULT
        {
            ToRelease<ICorDebugFunction> trFunc;
            IfFailRet(Evaluator::WalkMethods(pValue, true,
                [&](bool isStatic, const std::string &methodName, Evaluator::ReturnElementType &,
                    std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
                {
                    if (!isStatic || methodArgs.size() != 2 || opName != methodName || FAILED(cb(methodArgs)))
                    {
                        return S_OK; // Return with success to continue walk.
                    }

                    IfFailRet(getFunction(&trFunc));

                    return S_CAN_EXIT; // Fast exit from loop, since we already found trFunc.
                }));

            if (trFunc == nullptr)
            {
                return E_INVALIDARG;
            }

            std::array<ICorDebugValue *, 2> argsValue{pType1Value, pType2Value};
            return ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, nullptr, nullptr, argsValue.data(), 2, pResultValue);
        };

    // Try to execute operator for exact same type as provided values.
    if (SUCCEEDED(CallOperator([&](std::vector<SigElementType> &methodArgs) {
            return elemType1 != methodArgs.at(0).corType || typeName1 != methodArgs.at(0).typeName ||
                    elemType2 != methodArgs.at(1).corType || typeName2 != methodArgs.at(1).typeName
                    ? E_FAIL : S_OK;
        })))
    {
        return S_OK;
    }

    // Try to execute operator with implicit cast for second value.
    // Make sure we don't cast "base" struct/class value for this case,
    // since "... at least one parameter must have type T...".
    if (elemType == elemType1 && typeName == typeName1 &&
        SUCCEEDED(CallOperator([&](std::vector<SigElementType> &methodArgs) {
            if (elemType1 != methodArgs.at(0).corType || typeName1 != methodArgs.at(0).typeName)
            {
                return E_FAIL;
            }

            ToRelease<ICorDebugValue> trResultValue;
            if (FAILED(CallCastOperator("op_Implicit", pType1Value, methodArgs.at(1).corType, methodArgs.at(1).typeName,
                                        pType2Value, &trResultValue, ed)) &&
                FAILED(CallCastOperator("op_Implicit", pType2Value, methodArgs.at(1).corType, methodArgs.at(1).typeName,
                                        pType2Value, &trResultValue, ed)))
            {
                return E_FAIL;
            }

            IfFailRet(GetRealValueWithType(trResultValue, &trTypeValue));
            // The assignment modifies a local variable captured by reference, and
            // that modified value is used by CallOperator when calling EvalFunction.
            pType2Value = trTypeValue.GetPtr();

            return S_OK;
        })))
    {
        return S_OK;
    }

    // Try to execute operator with implicit cast for first value.
    return CallOperator([&](std::vector<SigElementType> &methodArgs) {
        if (elemType2 != methodArgs.at(1).corType || typeName2 != methodArgs.at(1).typeName)
        {
            return E_FAIL;
        }

        ToRelease<ICorDebugValue> trResultValue;
        if (FAILED(CallCastOperator("op_Implicit", pType1Value, methodArgs.at(0).corType, methodArgs.at(0).typeName,
                                    pType1Value, &trResultValue, ed)) &&
            FAILED(CallCastOperator("op_Implicit", pType2Value, methodArgs.at(0).corType, methodArgs.at(0).typeName,
                                    pType1Value, &trResultValue, ed)))
        {
            return E_FAIL;
        }

        trTypeValue.Free();
        IfFailRet(GetRealValueWithType(trResultValue, &trTypeValue));
        // The assignment modifies a local variable captured by reference, and
        // that modified value is used by CallOperator when calling EvalFunction.
        pType1Value = trTypeValue.GetPtr();

        return S_OK;
    });
}

HRESULT BinaryOperator(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue2;
    IfFailRet(GetFrontStackEntryValue(&trValue2, nullptr, evalStack, ed, output));
    evalStack.pop_front();
    ToRelease<ICorDebugValue> trRealValue2;
    CorElementType elemType2 = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue2, &trRealValue2, &elemType2));

    ToRelease<ICorDebugValue> trValue1;
    IfFailRet(GetFrontStackEntryValue(&trValue1, nullptr, evalStack, ed, output));
    evalStack.front().ResetEntry();
    ToRelease<ICorDebugValue> trRealValue1;
    CorElementType elemType1 = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue1, &trRealValue1, &elemType1));

    static std::unordered_map<Parser::SyntaxKind, std::pair<std::string, std::string>> opMap{
        {Parser::SyntaxKind::AddExpression, {"op_Addition", "+"}},
        {Parser::SyntaxKind::SubtractExpression, {"op_Subtraction", "-"}},
        {Parser::SyntaxKind::MultiplyExpression, {"op_Multiply", "*"}},
        {Parser::SyntaxKind::DivideExpression, {"op_Division", "/"}},
        {Parser::SyntaxKind::ModuloExpression, {"op_Modulus", "%"}},
        {Parser::SyntaxKind::RightShiftExpression, {"op_RightShift", ">>"}},
        {Parser::SyntaxKind::LeftShiftExpression, {"op_LeftShift", "<<"}},
        {Parser::SyntaxKind::LogicalAndExpression, {"op_LogicalAnd", "&&"}},
        {Parser::SyntaxKind::LogicalOrExpression, {"op_LogicalOr", "||"}},
        {Parser::SyntaxKind::ExclusiveOrExpression, {"op_ExclusiveOr", "^"}},
        {Parser::SyntaxKind::BitwiseAndExpression, {"op_BitwiseAnd", "&"}},
        {Parser::SyntaxKind::BitwiseOrExpression, {"op_BitwiseOr", "|"}},
        {Parser::SyntaxKind::EqualsExpression, {"op_Equality", "=="}},
        {Parser::SyntaxKind::NotEqualsExpression, {"op_Inequality", "!="}},
        {Parser::SyntaxKind::LessThanExpression, {"op_LessThan", "<"}},
        {Parser::SyntaxKind::GreaterThanExpression, {"op_GreaterThan", ">"}},
        {Parser::SyntaxKind::LessThanOrEqualExpression, {"op_LessThanOrEqual", "<="}},
        {Parser::SyntaxKind::GreaterThanOrEqualExpression, {"op_GreaterThanOrEqual", ">="}}};

    auto findOpName = opMap.find(opcode.kind);
    if (findOpName == opMap.end())
    {
        return E_FAIL;
    }

    auto fillErrorOutput = [&]() -> void
    {
        std::string typeName1 = "unknown";
        TypePrinter::GetTypeOfValue(trRealValue1, typeName1);
        std::string typeName2 = "unknown";
        TypePrinter::GetTypeOfValue(trRealValue2, typeName2);
        output = "error: Operator '" + findOpName->second.second +
                    "' cannot be applied to operands of type '" + typeName1 + "' and '" + typeName2 + "'";
    };

    if (elemType1 == ELEMENT_TYPE_VALUETYPE || elemType2 == ELEMENT_TYPE_VALUETYPE ||
        elemType1 == ELEMENT_TYPE_CLASS || elemType2 == ELEMENT_TYPE_CLASS)
    {
        if (((elemType1 == ELEMENT_TYPE_VALUETYPE || elemType1 == ELEMENT_TYPE_CLASS) &&
             SUCCEEDED(CallBinaryOperator(findOpName->second.first, trRealValue1, trRealValue1, trRealValue2,
                                          &evalStack.front().trValue, ed))) ||
            ((elemType2 == ELEMENT_TYPE_VALUETYPE || elemType2 == ELEMENT_TYPE_CLASS) &&
             SUCCEEDED(CallBinaryOperator(findOpName->second.first, trRealValue2, trRealValue1, trRealValue2,
                                          &evalStack.front().trValue, ed))))
        {
            return S_OK;
        }

        std::string typeRetName;
        CorElementType elemRetType = ELEMENT_TYPE_MAX;
        ToRelease<ICorDebugValue> trResultValue;
        // Try to implicitly cast struct/class object into primitive type or string.
        if ((PrimitiveTypes::IsPrimitiveType(elemType2) || elemType2 == ELEMENT_TYPE_STRING) && // First is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
            SUCCEEDED(GetArgData(trRealValue2, typeRetName, elemRetType)) &&
            SUCCEEDED(CallCastOperator("op_Implicit", trRealValue1, elemRetType, typeRetName, trRealValue1,
                                       &trResultValue, ed)))
        {
            trRealValue1.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue1, &elemType1));
            // goto string and primitive types (see code below this 'if' statement scope)
        }
        else if ((PrimitiveTypes::IsPrimitiveType(elemType1) || elemType1 == ELEMENT_TYPE_STRING) && // Second is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
                 SUCCEEDED(GetArgData(trRealValue1, typeRetName, elemRetType)) &&
                 SUCCEEDED(CallCastOperator("op_Implicit", trRealValue2, elemRetType, typeRetName, trRealValue2, &trResultValue, ed)))
        {
            trRealValue2.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue2, &elemType2));
            // goto string and primitive types (see code below this 'if' statement scope)
        }
        else
        {
            fillErrorOutput();
            return E_INVALIDARG;
        }
    }

    if ((elemType1 == ELEMENT_TYPE_STRING && (elemType2 == ELEMENT_TYPE_STRING || PrimitiveTypes::IsPrimitiveType(elemType2))) ||
        (elemType2 == ELEMENT_TYPE_STRING && (elemType1 == ELEMENT_TYPE_STRING || PrimitiveTypes::IsPrimitiveType(elemType1))))
    {
        auto getString = [](ICorDebugValue *pValue, CorElementType elemType, std::string &strValue) -> HRESULT
        {
            HRESULT Status = S_OK;
            BOOL isNull = FALSE;
            ToRelease<ICorDebugValue> trStrValue;
            if (elemType == ELEMENT_TYPE_STRING)
            {
                IfFailRet(DereferenceAndUnboxValue(pValue, &trStrValue, &isNull));
                if (isNull == TRUE)
                {
                    return S_OK;
                }
                return PrintStringValue(trStrValue, strValue);
            }

            PrimitiveTypes::PrimitiveValue primValue;
            PrimitiveTypes::GetPrimitiveData(pValue, primValue);
            strValue = PrimitiveTypes::ToString(primValue);
            return S_OK;
        };

        std::string string1;
        IfFailRet(getString(trRealValue1, elemType1, string1));
        std::string string2;
        IfFailRet(getString(trRealValue2, elemType2, string2));

        if ((elemType1 == ELEMENT_TYPE_STRING && elemType2 == ELEMENT_TYPE_STRING) &&
            (opcode.kind == Parser::SyntaxKind::EqualsExpression || opcode.kind == Parser::SyntaxKind::NotEqualsExpression))
        {
            PrimitiveTypes::PrimitiveValue result =
                opcode.kind == Parser::SyntaxKind::EqualsExpression ? string1 == string2 : string1 != string2;
            return PrimitiveTypes::CreateICorValue(ed.pThread, result, &evalStack.front().trValue);
        }
        else if (opcode.kind == Parser::SyntaxKind::AddExpression)
        {
            return ed.pEvalHelpers->CreateString(ed.pThread, string1 + string2, &evalStack.front().trValue);
        }
        else if (elemType1 != ELEMENT_TYPE_BOOLEAN &&
                 (opcode.kind == Parser::SyntaxKind::LogicalAndExpression ||
                  opcode.kind == Parser::SyntaxKind::LogicalOrExpression))
        {
            std::string typeName = "unknown";
            TypePrinter::GetTypeOfValue(trRealValue1, typeName);
            output = "error: Cannot implicitly convert type '" + typeName + "' to 'bool'";
            return E_INVALIDARG;
        }
        else
        {
            fillErrorOutput();
            return E_INVALIDARG;
        }
    }

    if (!PrimitiveTypes::IsPrimitiveType(elemType1) || !PrimitiveTypes::IsPrimitiveType(elemType2))
    {
        fillErrorOutput();
        return E_INVALIDARG;
    }

    return PrimitiveTypes::CalculateBinary(opcode.kind, ed.pThread, trRealValue1, trRealValue2, &evalStack.front().trValue, output);
}

HRESULT UnaryOperator(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(GetFrontStackEntryValue(&trValue, nullptr, evalStack, ed, output));
    evalStack.front().ResetEntry(EvalStackEntry::ResetLiteralStatus::No);
    ToRelease<ICorDebugValue> trRealValue;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue, &trRealValue, &elemType));

    static const std::unordered_map<Parser::SyntaxKind, std::pair<std::string, std::string>> opMap{
        {Parser::SyntaxKind::LogicalNotExpression, {"op_LogicalNot", "!"}},
        {Parser::SyntaxKind::BitwiseNotExpression, {"op_OnesComplement", "~"}},
        {Parser::SyntaxKind::UnaryPlusExpression, {"op_UnaryPlus", "+"}},
        {Parser::SyntaxKind::UnaryMinusExpression, {"op_UnaryNegation", "-"}}};

    auto findOpName = opMap.find(opcode.kind);
    if (findOpName == opMap.end())
    {
        return E_FAIL;
    }

    auto fillErrorOutput = [&]() -> void
    {
        std::string typeName = "unknown";
        TypePrinter::GetTypeOfValue(trRealValue, typeName);
        output = "error: Operator '" + findOpName->second.second + "' cannot be applied to operand of type '" + typeName + "'";
    };

    if (elemType == ELEMENT_TYPE_VALUETYPE || elemType == ELEMENT_TYPE_CLASS)
    {
        if (SUCCEEDED(CallUnaryOperator(findOpName->second.first, trRealValue, &evalStack.front().trValue, ed)))
        {
            return S_OK;
        }
        else
        {
            fillErrorOutput();
            return E_INVALIDARG;
        }
    }
    else if (!PrimitiveTypes::IsPrimitiveType(elemType))
    {
        fillErrorOutput();
        return E_INVALIDARG;
    }

    return PrimitiveTypes::CalculateUnary(opcode.kind, ed.pThread, trRealValue, &evalStack.front().trValue, output);
}

HRESULT IdentifierName(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &/*ed*/)
{
    std::string argString = opcode.str;
    ReplaceInternalNames(argString, true);

    evalStack.emplace_front();
    evalStack.front().identifiers.emplace_back(std::move(argString));
    evalStack.front().editable = true;
    return S_OK;
}

HRESULT GenericName(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    const uint32_t argCount = opcode.count;
    std::string argString = opcode.str;
    std::vector<ToRelease<ICorDebugType>> trGenericValues;
    std::string generics = ">";
    trGenericValues.reserve(argCount);
    for (uint32_t i = 0; i < argCount; i++)
    {
        ToRelease<ICorDebugValue> trValue;
        ToRelease<ICorDebugType> trType;
        ToRelease<ICorDebugValue2> trValue2;
        std::string genericType;
        Status = GetFrontStackEntryValue(&trValue, nullptr, evalStack, ed, output);
        if (Status == S_OK)
        {
            IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
            IfFailRet(trValue2->GetExactType(&trType));
        }
        else
        {
            IfFailRet(GetFrontStackEntryType(&trType, evalStack, ed, output));
        }
        TypePrinter::GetTypeOfValue(trType, genericType);
        generics.insert(0, "," + genericType);
        trGenericValues.emplace_back(trType.Detach());
        evalStack.pop_front();
    }
    generics.erase(0, 1);
    argString += "<" + generics;
    evalStack.emplace_front();
    evalStack.front().identifiers.emplace_back(std::move(argString));
    evalStack.front().trGenericTypeCache = std::move(trGenericValues);
    evalStack.front().editable = true;
    return S_OK;
}

HRESULT InvocationExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    const uint32_t argCount = opcode.count;

    HRESULT Status = S_OK;
    bool idsEmpty = false;
    bool isInstance = true;
    std::vector<ToRelease<ICorDebugValue>> trArgs(argCount);
    uint32_t tmpArgCount = argCount;
    while (tmpArgCount > 0)
    {
        tmpArgCount--;
        IfFailRet(GetFrontStackEntryValue(&trArgs.at(tmpArgCount), nullptr, evalStack, ed, output));
        evalStack.pop_front();
    }

    if (evalStack.front().preventBinding)
    {
        return S_OK;
    }

    assert(!evalStack.front().identifiers.empty()); // We must have at least method name (identifier).

    // TODO local defined function (compiler will create such function with name like `<Calc1>g__Calc2|0_0`)
    const std::string funcNameGenerics = evalStack.front().identifiers.back();
    evalStack.front().identifiers.pop_back();

    std::string funcName;
    const std::vector<std::string> methodGenericStrings = EvalUtils::ParseGenericParams(funcNameGenerics, funcName);
    const size_t pos = funcName.find('`');
    if (pos != std::string::npos)
    {
        funcName.resize(pos);
    }

    if ((evalStack.front().trValue == nullptr) && evalStack.front().identifiers.empty())
    {
        std::string methodClass;
        idsEmpty = true;
        IfFailRet(ed.pEvaluator->GetMethodClass(ed.pThread, ed.frameLevel, methodClass, isInstance));
        if (isInstance)
        {
            evalStack.front().identifiers.emplace_back("this");
        }
        else
        {
            // here we add a full qualified "path" separated with dots (aka Class.Subclass.Subclass ..etc)
            // although <identifiers> usually contains a vector of components of the full name qualification
            // Anyway, our added component will be correctly processed by Evaluator::ResolveIdentifiers() for
            // that case as it seals all the qualification components into one with dots before using them.
            evalStack.front().identifiers.emplace_back(methodClass);
        }
    }

    ToRelease<ICorDebugValue> trValue;
    ToRelease<ICorDebugType> trType;
    IfFailRet(ed.pEvaluator->ResolveIdentifiers(ed.pThread, ed.frameLevel, evalStack.front().trValue, nullptr,
                                                evalStack.front().identifiers, &trValue, nullptr, &trType));

    bool searchStatic = false;
    if (trType != nullptr)
    {
        searchStatic = true;
    }
    else
    {
        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(trValue->GetType(&elemType));

        // Boxing built-in element type into value type in order to call methods.
        auto entry = ed.trElementToValueClassMap.find(elemType);
        if (entry != ed.trElementToValueClassMap.end())
        {
            uint32_t cbSize = 0;
            IfFailRet(trValue->GetSize(&cbSize));
            std::vector<uint8_t> elemValue(cbSize, 0);

            ToRelease<ICorDebugGenericValue> trGenericValue;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
            IfFailRet(trGenericValue->GetValue(static_cast<void *>(elemValue.data())));

            trValue.Free();
            IfFailRet(CreateValueType(ed.pEvalWaiter, ed.pThread, entry->second, &trValue, elemValue.data()));
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        IfFailRet(trValue2->GetExactType(&trType));
    }

    std::vector<SigElementType> funcArgs(argCount);
    for (uint32_t i = 0; i < argCount; ++i)
    {
        ToRelease<ICorDebugValue> trValueArg;
        IfFailRet(DereferenceAndUnboxValue(trArgs.at(i).GetPtr(), &trValueArg, nullptr));
        IfFailRet(trValueArg->GetType(&funcArgs.at(i).corType));

        if (funcArgs.at(i).corType == ELEMENT_TYPE_VALUETYPE || funcArgs.at(i).corType == ELEMENT_TYPE_CLASS)
        {
            IfFailRet(TypePrinter::NameForTypeByValue(trValueArg, funcArgs.at(i).typeName));
        }
        else if (funcArgs.at(i).corType == ELEMENT_TYPE_SZARRAY || funcArgs.at(i).corType == ELEMENT_TYPE_ARRAY)
        {
            IfFailRet(TypePrinter::GetTypeOfValue(trValueArg, funcArgs.at(i).typeName));
        }
    }

    std::vector<SigElementType> methodGenerics;
    methodGenerics.reserve(methodGenericStrings.size());
    std::transform(methodGenericStrings.begin(), methodGenericStrings.end(),
                   std::back_inserter(methodGenerics), [](const auto &methodGenericString)
                   {
                       return dncdbg::Evaluator::GetElementTypeByTypeName(methodGenericString);
                   });

    ToRelease<ICorDebugFunction> trFunc;
    ToRelease<ICorDebugType> trResultType;
    IfFailRet(Evaluator::WalkMethods(trType, true, &trResultType,
        [&](bool isStatic, const std::string &methodName, Evaluator::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if ((searchStatic && !isStatic) || (!searchStatic && isStatic && !idsEmpty) ||
                funcArgs.size() != methodArgs.size() || funcName != methodName)
            {
                return S_OK; // Return with success to continue walk.
            }

            for (size_t i = 0; i < funcArgs.size(); ++i)
            {
                if (FAILED(ApplyMethodGenerics(methodGenerics, methodArgs.at(i))) ||
                    funcArgs.at(i) != methodArgs.at(i))
                {
                    return S_OK; // Return with success to continue walk.
                }
            }

            IfFailRet(getFunction(&trFunc));
            isInstance = !isStatic;

            return S_CAN_EXIT; // Fast exit from loop.
        }));

    if (trFunc == nullptr)
    {
        if (SUCCEEDED(Evaluator::LookupExtensionMethods(ed.pThread, trType, funcName, funcArgs, methodGenerics, &trFunc)))
        {
            isInstance = true; // Extension methods always require "this" as their first parameter
        }
        else
        {
            return E_FAIL;
        }
    }

    if (trFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    if (trResultType != nullptr)
    {
        trType = trResultType.Detach();
    }

    const uint32_t realArgsCount = argCount + (isInstance ? 1 : 0);
    std::vector<ICorDebugValue *> pValueArgs;
    pValueArgs.reserve(realArgsCount);

    // Place instance value ("this") if extension or not static method
    if (isInstance)
    {
        pValueArgs.emplace_back(trValue.GetPtr());
    }

    // Add arguments values
    for (uint32_t i = 0; i < argCount; i++)
    {
        pValueArgs.emplace_back(trArgs.at(i).GetPtr());
    }

    // Prepare method's generic types if any
    std::vector<ToRelease<ICorDebugType>> trMethodGenericTypes;
    trMethodGenericTypes.reserve(evalStack.front().trGenericTypeCache.size());
    for (size_t i = evalStack.front().trGenericTypeCache.size(); i > 0; i--)
    {
        // Note: we detach here, since trGenericTypeCache will be cleared at ResetEntry() call anyway.
        trMethodGenericTypes.emplace_back(evalStack.front().trGenericTypeCache.at(i - 1).Detach());
    }

    evalStack.front().ResetEntry();
    Status = ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, trType.GetPtr(), trMethodGenericTypes.empty() ? nullptr : &trMethodGenericTypes,
                                           pValueArgs.data(), static_cast<uint32_t>(pValueArgs.size()), &evalStack.front().trValue);

    // CORDBG_S_FUNC_EVAL_HAS_NO_RESULT: Some Func evals will lack a return value, such as those whose return type is void.
    if (Status == CORDBG_S_FUNC_EVAL_HAS_NO_RESULT)
    {
        // We can't create ELEMENT_TYPE_VOID, so, we are forced to use System.Void instead.
        IfFailRet(CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trVoidClass, &evalStack.front().trValue, nullptr));
    }

    return Status;
}

HRESULT ElementAccessExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    const uint32_t argCount = opcode.count;
    HRESULT Status = S_OK;

    std::vector<ToRelease<ICorDebugValue>> trIndexValues(argCount);

    uint32_t tmpArgCount = opcode.count;
    while (tmpArgCount > 0)
    {
        tmpArgCount--;
        IfFailRet(GetFrontStackEntryValue(&trIndexValues.at(tmpArgCount), nullptr, evalStack, ed, output));
        evalStack.pop_front();
    }
    if (evalStack.front().preventBinding)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue> trObjectValue;
    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(GetFrontStackEntryValue(&trObjectValue, &setterData, evalStack, ed, output));

    ToRelease<ICorDebugValue> trRealValue;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trObjectValue, &trRealValue, &elemType));

    if (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
    {
        std::vector<uint32_t> indexes;
        uint32_t tmpArgCount = opcode.count;
        while (tmpArgCount > 0)
        {
            tmpArgCount--;
            uint32_t index = 0;
            // ICorDebugArrayValue::GetElement expects uint32_t indices
            IfFailRet(PrimitiveTypes::ForceCastToUint(trIndexValues.at(tmpArgCount), index));
            indexes.insert(indexes.begin(), index);
        }
        evalStack.front().trValue.Free();
        evalStack.front().identifiers.clear();
        evalStack.front().setterData = std::move(setterData);
        Status = dncdbg::Evaluator::GetElement(trObjectValue, indexes, &evalStack.front().trValue);
    }
    else
    {
        std::vector<SigElementType> funcArgs(argCount);
        for (uint32_t i = 0; i < argCount; ++i)
        {
            ToRelease<ICorDebugValue> trValueArg;
            IfFailRet(DereferenceAndUnboxValue(trIndexValues.at(i).GetPtr(), &trValueArg, nullptr));
            IfFailRet(trValueArg->GetType(&funcArgs.at(i).corType));

            if (funcArgs.at(i).corType == ELEMENT_TYPE_VALUETYPE || funcArgs.at(i).corType == ELEMENT_TYPE_CLASS)
            {
                IfFailRet(TypePrinter::NameForTypeByValue(trValueArg, funcArgs.at(i).typeName));
            }
        }

        ToRelease<ICorDebugFunction> trFunc;
        IfFailRet(Evaluator::WalkMethods(trObjectValue, true,
            [&](bool, const std::string &methodName, Evaluator::ReturnElementType &retType,
                std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
            {
                const std::string name = "get_Item";
                const std::size_t found = methodName.rfind(name);
                if (retType.corType == ELEMENT_TYPE_VOID || found == std::string::npos ||
                    found != methodName.length() - name.length() || funcArgs.size() != methodArgs.size())
                {
                    return S_OK; // Return with success to continue walk.
                }

                for (size_t i = 0; i < funcArgs.size(); ++i)
                {
                    if (funcArgs.at(i).corType != methodArgs.at(i).corType || funcArgs.at(i).typeName != methodArgs.at(i).typeName)
                    {
                        return S_OK; // Return with success to continue walk.
                    }
                }
                IfFailRet(getFunction(&trFunc));
                return S_CAN_EXIT; // Fast exit from loop, since we already found trFunc.
            }));

        if (trFunc == nullptr)
        {
            return E_INVALIDARG;
        }

        evalStack.front().ResetEntry();
        std::vector<ICorDebugValue *> trValueArgs;
        trValueArgs.reserve(argCount + 1);

        trValueArgs.emplace_back(trObjectValue.GetPtr());

        for (uint32_t i = 0; i < argCount; i++)
        {
            trValueArgs.emplace_back(trIndexValues.at(i).GetPtr());
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trObjectValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));

        Status = ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, trType.GetPtr(), nullptr, trValueArgs.data(),
                                               argCount + 1, &evalStack.front().trValue);
    }
    return Status;
}

HRESULT ElementBindingExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    const uint32_t argCount = opcode.count;
    HRESULT Status = S_OK;

    std::vector<ToRelease<ICorDebugValue>> trIndexValues(argCount);

    uint32_t tmpArgCount = argCount;
    while (tmpArgCount > 0)
    {
        tmpArgCount--;
        IfFailRet(GetFrontStackEntryValue(&trIndexValues.at(tmpArgCount), nullptr, evalStack, ed, output));
        evalStack.pop_front();
    }
    if (evalStack.front().preventBinding)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue> trObjectValue;
    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(GetFrontStackEntryValue(&trObjectValue, &setterData, evalStack, ed, output));

    ToRelease<ICorDebugReferenceValue> trReferenceValue;
    IfFailRet(trObjectValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue)));
    BOOL isNull = FALSE;
    IfFailRet(trReferenceValue->IsNull(&isNull));

    if (isNull == TRUE)
    {
        evalStack.front().preventBinding = true;
        return S_OK;
    }

    ToRelease<ICorDebugValue> trRealValue;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trObjectValue, &trRealValue, &elemType));

    if (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
    {
        std::vector<uint32_t> indexes;
        uint32_t tmpArgCount = argCount;
        while (tmpArgCount > 0)
        {
            tmpArgCount--;
            uint32_t index = 0;
            // ICorDebugArrayValue::GetElement expects uint32_t indices
            IfFailRet(PrimitiveTypes::ForceCastToUint(trIndexValues.at(tmpArgCount), index));
            indexes.insert(indexes.begin(), index);
        }
        evalStack.front().trValue.Free();
        evalStack.front().identifiers.clear();
        evalStack.front().setterData = std::move(setterData);
        Status = dncdbg::Evaluator::GetElement(trObjectValue, indexes, &evalStack.front().trValue);
    }
    else
    {
        std::vector<SigElementType> funcArgs(argCount);
        for (uint32_t i = 0; i < argCount; ++i)
        {
            ToRelease<ICorDebugValue> trValueArg;
            IfFailRet(DereferenceAndUnboxValue(trIndexValues.at(i).GetPtr(), &trValueArg, nullptr));
            IfFailRet(trValueArg->GetType(&funcArgs.at(i).corType));

            if (funcArgs.at(i).corType == ELEMENT_TYPE_VALUETYPE || funcArgs.at(i).corType == ELEMENT_TYPE_CLASS)
            {
                IfFailRet(TypePrinter::NameForTypeByValue(trValueArg, funcArgs.at(i).typeName));
            }
        }

        ToRelease<ICorDebugFunction> trFunc;
        IfFailRet(Evaluator::WalkMethods(trObjectValue, true,
            [&](bool, const std::string &methodName, Evaluator::ReturnElementType &retType,
                std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
            {
                const std::string name = "get_Item";
                const std::size_t found = methodName.rfind(name);
                if (retType.corType == ELEMENT_TYPE_VOID || found == std::string::npos ||
                    found != methodName.length() - name.length() || funcArgs.size() != methodArgs.size())
                {
                    return S_OK; // Return with success to continue walk.
                }

                for (size_t i = 0; i < funcArgs.size(); ++i)
                {
                    if (funcArgs.at(i).corType != methodArgs.at(i).corType || funcArgs.at(i).typeName != methodArgs.at(i).typeName)
                    {
                        return S_OK; // Return with success to continue walk.
                    }
                }
                IfFailRet(getFunction(&trFunc));
                return S_CAN_EXIT; // Fast exit from loop, since we already found trFunc.
            }));

        if (trFunc == nullptr)
        {
            return E_INVALIDARG;
        }

        evalStack.front().ResetEntry();
        std::vector<ICorDebugValue *> trValueArgs;
        trValueArgs.reserve(argCount + 1);

        trValueArgs.emplace_back(trObjectValue.GetPtr());

        for (uint32_t i = 0; i < argCount; i++)
        {
            trValueArgs.emplace_back(trIndexValues.at(i).GetPtr());
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trObjectValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));

        Status = ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, trType.GetPtr(), nullptr, trValueArgs.data(),
                                               argCount + 1, &evalStack.front().trValue);
    }
    return Status;
}

HRESULT NumericLiteralExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    const bool argRealLiteral = (opcode.count == 1);
    const std::string argString = opcode.str;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    std::vector<uint8_t> data;

    HRESULT Status = S_OK;
    IfFailRet(Parser::DetermineNumericTypeAndData(argString, argRealLiteral, elemType, data, output));

    evalStack.emplace_front();
    evalStack.front().literal = true;
    if (elemType == ELEMENT_TYPE_VALUETYPE)
    {
        return CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trDecimalClass, &evalStack.front().trValue, data.data());
    }
    else
    {
        return PrimitiveTypes::CreateICorValue(ed.pThread, elemType, data.data(), &evalStack.front().trValue);
    }
}

HRESULT StringLiteralExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &ed)
{
    std::string argString = opcode.str;
    ReplaceInternalNames(argString, true);
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return ed.pEvalHelpers->CreateString(ed.pThread, argString, &evalStack.front().trValue);
}

HRESULT CharacterLiteralExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    WSTRING wStr = to_utf16(opcode.str);
    if (wStr.empty() || wStr.size() > 1)
    {
        output = "Failed to parse character.";
        return E_INVALIDARG;
    }

    WCHAR value = wStr.at(0);
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return PrimitiveTypes::CreateICorValue(ed.pThread, ELEMENT_TYPE_CHAR, &value, &evalStack.front().trValue);
}

HRESULT PredefinedType(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(Parser::ParsePredefinedType(opcode.str, elemType, output));

    evalStack.emplace_front();

    if (elemType == ELEMENT_TYPE_VALUETYPE)
    {
        return CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trDecimalClass, &evalStack.front().trValue, nullptr);
    }
    else if (elemType == ELEMENT_TYPE_STRING)
    {
        const std::string emptyString;
        return ed.pEvalHelpers->CreateString(ed.pThread, emptyString, &evalStack.front().trValue);
    }
    else
    {
        return PrimitiveTypes::CreateICorValue(ed.pThread, elemType, nullptr, &evalStack.front().trValue);
    }
}

HRESULT MemberBindingExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    assert(evalStack.size() > 1);
    assert(evalStack.front().identifiers.size() == 1); // Only one unresolved identifier must be here.
    assert(!evalStack.front().trValue);              // Should be unresolved identifier only front element.

    std::string identifier = std::move(evalStack.front().identifiers.at(0));
    evalStack.pop_front();

    if (evalStack.front().preventBinding)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(GetFrontStackEntryValue(&trValue, &setterData, evalStack, ed, output));
    evalStack.front().trValue = trValue.Detach();
    evalStack.front().identifiers.clear();
    evalStack.front().setterData = std::move(setterData);

    ToRelease<ICorDebugReferenceValue> trReferenceValue;
    IfFailRet(evalStack.front().trValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue)));
    BOOL isNull = FALSE;
    IfFailRet(trReferenceValue->IsNull(&isNull));

    if (isNull == TRUE)
    {
        evalStack.front().preventBinding = true;
    }
    else
    {
        evalStack.front().identifiers.emplace_back(std::move(identifier));
    }

    return S_OK;
}

HRESULT SimpleMemberAccessExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &/*ed*/)
{
    assert(evalStack.size() > 1);
    assert(!evalStack.front().trValue);                // Should be unresolved identifier only front element.
    assert(evalStack.front().identifiers.size() == 1); // Only one unresolved identifier must be here.

    std::string identifier = std::move(evalStack.front().identifiers.at(0));
    std::vector<ToRelease<ICorDebugType>> trDebugTypes;
    const size_t genericsCount = evalStack.front().trGenericTypeCache.size();
    if (genericsCount > 0)
    {
        trDebugTypes = std::move(evalStack.front().trGenericTypeCache);
    }
    evalStack.pop_front();
    if (!evalStack.front().preventBinding)
    {
        evalStack.front().identifiers.emplace_back(std::move(identifier));
        evalStack.front().trGenericTypeCache.clear(); // We need method's generics only, so remove all previous if exist.
        if (genericsCount > 0)
        {
            evalStack.front().trGenericTypeCache = std::move(trDebugTypes);
        }
    }
    return S_OK;
}

HRESULT QualifiedName(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    return SimpleMemberAccessExpression(opcode, evalStack, output, ed);
}

HRESULT TrueLiteralExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateBooleanValue(ed.pThread, &evalStack.front().trValue, true);
}

HRESULT FalseLiteralExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateBooleanValue(ed.pThread, &evalStack.front().trValue, false);
}

HRESULT NullLiteralExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateNullValue(ed.pThread, &evalStack.front().trValue);
}

HRESULT SizeOfExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    assert(!evalStack.empty());
    HRESULT Status = S_OK;
    uint32_t size = 0;
    void *szPtr = &size;

    if (evalStack.front().trValue != nullptr)
    {
        //  predefined type
        CorElementType elType = ELEMENT_TYPE_MAX;
        IfFailRet(evalStack.front().trValue->GetType(&elType));
        if (elType == ELEMENT_TYPE_CLASS)
        {
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(DereferenceAndUnboxValue(evalStack.front().trValue, &trValue, nullptr));
            IfFailRet(trValue->GetSize(&size));
        }
        else
        {
            IfFailRet(evalStack.front().trValue->GetSize(&size));
        }
    }
    else
    {
        ToRelease<ICorDebugType> trType;
        ToRelease<ICorDebugValue> trValue;
        ToRelease<ICorDebugValue> trValueRef;

        IfFailRet(GetFrontStackEntryType(&trType, evalStack, ed, output));
        if (trType != nullptr)
        {
            CorElementType elType = ELEMENT_TYPE_MAX;
            IfFailRet(trType->GetType(&elType));
            if (elType == ELEMENT_TYPE_VALUETYPE)
            {
                // user defined type (structure)
                ToRelease<ICorDebugClass> trClass;

                IfFailRet(trType->GetClass(&trClass));
                IfFailRet(CreateValueType(ed.pEvalWaiter, ed.pThread, trClass, &trValueRef, nullptr));
                IfFailRet(DereferenceAndUnboxValue(trValueRef, &trValue, nullptr));
                IfFailRet(trValue->GetSize(&size));
            }
            else
            {
                return E_INVALIDARG;
            }
        }
        else
        {
            // TODO other cases
            return E_NOTIMPL;
        }
    }
    evalStack.front().ResetEntry();
    return PrimitiveTypes::CreateICorValue(ed.pThread, ELEMENT_TYPE_U4, szPtr, &evalStack.front().trValue);
}

HRESULT CoalesceExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trRealValueRightOp;
    ToRelease<ICorDebugValue> trRightOpValue;
    CorElementType elemTypeRightOp = ELEMENT_TYPE_MAX;
    IfFailRet(GetFrontStackEntryValue(&trRightOpValue, nullptr, evalStack, ed, output));
    IfFailRet(GetRealValueWithType(trRightOpValue, &trRealValueRightOp, &elemTypeRightOp));
    auto rightOperand = std::move(evalStack.front());
    evalStack.pop_front();

    ToRelease<ICorDebugValue> trRealValueLeftOp;
    ToRelease<ICorDebugValue> trLeftOpValue;
    CorElementType elemTypeLeftOp = ELEMENT_TYPE_MAX;
    IfFailRet(GetFrontStackEntryValue(&trLeftOpValue, nullptr, evalStack, ed, output));
    IfFailRet(GetRealValueWithType(trLeftOpValue, &trRealValueLeftOp, &elemTypeLeftOp));
    std::string typeNameLeft;
    std::string typeNameRight;

    // TODO implement nullable type support

    // TODO add implementation for object type ?? other
    if ((elemTypeRightOp == ELEMENT_TYPE_STRING && elemTypeLeftOp == ELEMENT_TYPE_STRING) ||
        ((elemTypeRightOp == ELEMENT_TYPE_CLASS && elemTypeLeftOp == ELEMENT_TYPE_CLASS) &&
         SUCCEEDED(TypePrinter::NameForTypeByValue(trRealValueLeftOp, typeNameLeft)) &&
         SUCCEEDED(TypePrinter::NameForTypeByValue(trRealValueRightOp, typeNameRight)) &&
         typeNameLeft == typeNameRight))
    {
        ToRelease<ICorDebugReferenceValue> trRefValue;
        IfFailRet(trLeftOpValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trRefValue)));
        BOOL isNull = FALSE;
        IfFailRet(trRefValue->IsNull(&isNull));

        if (isNull == TRUE)
        {
            evalStack.pop_front();
            evalStack.push_front(std::move(rightOperand));
        }
        return S_OK;
    }
    // TODO add processing for parent-child class relationship
    std::string typeName1;
    std::string typeName2;
    IfFailRet(TypePrinter::GetTypeOfValue(trRealValueLeftOp, typeName1));
    IfFailRet(TypePrinter::GetTypeOfValue(trRealValueRightOp, typeName2));
    output = "error CS0019: Operator ?? cannot be applied to operands of type '" + typeName1 + "' and '" + typeName2 + "'";
    return E_INVALIDARG;
}

HRESULT ThisExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &/*output*/, EvalData &/*ed*/)
{
    evalStack.emplace_front();
    evalStack.front().identifiers.emplace_back("this");
    evalStack.front().editable = true;
    return S_OK;
}

} // unnamed namespace

HRESULT EvalStackMachine::Run(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &expression,
                              std::list<EvalStackEntry> &evalStack, std::string &output)
{
    static const std::unordered_map<Parser::SyntaxKind, std::function<HRESULT(const Parser::Opcode &, std::list<EvalStackEntry> &, std::string &, EvalData &)>> CommandImplementation{
        {Parser::SyntaxKind::IdentifierName, IdentifierName},
        {Parser::SyntaxKind::GenericName, GenericName},
        {Parser::SyntaxKind::InvocationExpression, InvocationExpression},
        {Parser::SyntaxKind::ElementAccessExpression, ElementAccessExpression},
        {Parser::SyntaxKind::ElementBindingExpression, ElementBindingExpression},
        {Parser::SyntaxKind::NumericLiteralExpression, NumericLiteralExpression},
        {Parser::SyntaxKind::StringLiteralExpression, StringLiteralExpression},
        {Parser::SyntaxKind::CharacterLiteralExpression, CharacterLiteralExpression},
        {Parser::SyntaxKind::PredefinedType, PredefinedType},
        {Parser::SyntaxKind::QualifiedName, QualifiedName},
        {Parser::SyntaxKind::MemberBindingExpression, MemberBindingExpression},
        {Parser::SyntaxKind::SimpleMemberAccessExpression, SimpleMemberAccessExpression},
        {Parser::SyntaxKind::AddExpression, BinaryOperator},
        {Parser::SyntaxKind::MultiplyExpression, BinaryOperator},
        {Parser::SyntaxKind::SubtractExpression, BinaryOperator},
        {Parser::SyntaxKind::DivideExpression, BinaryOperator},
        {Parser::SyntaxKind::ModuloExpression, BinaryOperator},
        {Parser::SyntaxKind::LeftShiftExpression, BinaryOperator},
        {Parser::SyntaxKind::RightShiftExpression, BinaryOperator},
        {Parser::SyntaxKind::BitwiseAndExpression, BinaryOperator},
        {Parser::SyntaxKind::BitwiseOrExpression, BinaryOperator},
        {Parser::SyntaxKind::ExclusiveOrExpression, BinaryOperator},
        {Parser::SyntaxKind::LogicalAndExpression, BinaryOperator},
        {Parser::SyntaxKind::LogicalOrExpression, BinaryOperator},
        {Parser::SyntaxKind::EqualsExpression, BinaryOperator},
        {Parser::SyntaxKind::NotEqualsExpression, BinaryOperator},
        {Parser::SyntaxKind::GreaterThanExpression, BinaryOperator},
        {Parser::SyntaxKind::LessThanExpression, BinaryOperator},
        {Parser::SyntaxKind::GreaterThanOrEqualExpression, BinaryOperator},
        {Parser::SyntaxKind::LessThanOrEqualExpression, BinaryOperator},
        {Parser::SyntaxKind::UnaryPlusExpression, UnaryOperator},
        {Parser::SyntaxKind::UnaryMinusExpression, UnaryOperator},
        {Parser::SyntaxKind::LogicalNotExpression, UnaryOperator},
        {Parser::SyntaxKind::BitwiseNotExpression, UnaryOperator},
        {Parser::SyntaxKind::TrueLiteralExpression, TrueLiteralExpression},
        {Parser::SyntaxKind::FalseLiteralExpression, FalseLiteralExpression},
        {Parser::SyntaxKind::NullLiteralExpression, NullLiteralExpression},
        {Parser::SyntaxKind::SizeOfExpression, SizeOfExpression},
        {Parser::SyntaxKind::CoalesceExpression, CoalesceExpression},
        {Parser::SyntaxKind::ThisExpression, ThisExpression}
    };

    // Note, internal variables start with "$" and must be replaced before CSharp syntax analyzer.
    // This data will be restored after CSharp syntax analyzer in IdentifierName and StringLiteralExpression.
    std::string fixed_expression = expression;
    ReplaceInternalNames(fixed_expression);

    HRESULT Status = S_OK;
    std::list<Parser::Opcode> stackProgram;
    IfFailRet(Parser::GenerateProgram(fixed_expression, stackProgram, output));

    m_evalData.pThread = pThread;
    m_evalData.frameLevel = frameLevel;

    for (const auto &executionStep : stackProgram)
    {
        auto findStep = CommandImplementation.find(executionStep.kind);
        if (findStep == CommandImplementation.end())
        {
            output = "Invalid syntax kind.";
            Status = E_INVALIDARG;
            break;
        }

        if (FAILED(Status = findStep->second(executionStep, evalStack, output, m_evalData)))
        {
            break;
        }
    }

    switch (Status)
    {
    case CORDBG_E_ILLEGAL_IN_PROLOG:
    case CORDBG_E_ILLEGAL_IN_NATIVE_CODE:
    case CORDBG_E_ILLEGAL_IN_STACK_OVERFLOW:
    case CORDBG_E_ILLEGAL_IN_OPTIMIZED_CODE:
    case CORDBG_E_ILLEGAL_AT_GC_UNSAFE_POINT:
        output = "This expression causes side effects and will not be evaluated.";
        LOGE(log << "Eval error: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        break;
    case COR_E_TIMEOUT:
        output = "Evaluation timed out.";
        break;
    case COR_E_OPERATIONCANCELED:
        output = "Evaluation canceled by user.";
        break;
    case CORDBG_E_CANT_CALL_ON_THIS_THREAD:
        output = "The function evaluation requires all threads to run.";
        break;
    case E_UNEXPECTED:
        output = "Evaluation timed out, but function evaluation can't be completed or aborted. Debuggee has inconsistent state now.";
        break;
    case COR_E_THREADSTATE:
        output = "Thread is in an invalid state for this operation.";
        break;
    case CORDBG_E_DEBUGGING_DISABLED:
        output = "Implicit function evaluation is turned off by the user.";
        break;
    default:
        break;
    }

    return Status;
}

HRESULT EvalStackMachine::EvaluateExpression(ICorDebugThread *pThread, FrameLevel frameLevel,
                                             const std::string &expression, ICorDebugValue **ppResultValue,
                                             std::string &output, bool *editable,
                                             std::unique_ptr<Evaluator::SetterData> *resultSetterData)
{
    HRESULT Status = S_OK;
    std::list<EvalStackEntry> evalStack;
    IfFailRet(Run(pThread, frameLevel, expression, evalStack, output));

    assert(evalStack.size() == 1);

    std::unique_ptr<Evaluator::SetterData> setterData;
    IfFailRet(GetFrontStackEntryValue(ppResultValue, &setterData, evalStack, m_evalData, output));

    if (editable != nullptr)
    {
        *editable = (setterData != nullptr) && (setterData->trSetterFunction == nullptr) ? false /*property don't have setter*/
                                                                                         : evalStack.front().editable;
    }

    if (resultSetterData != nullptr)
    {
        *resultSetterData = std::move(setterData);
    }

    return S_OK;
}

HRESULT EvalStackMachine::SetValueByExpression(ICorDebugThread *pThread, FrameLevel frameLevel,
                                               ICorDebugValue *pValue, const std::string &expression, std::string &output)
{
    HRESULT Status = S_OK;
    std::list<EvalStackEntry> evalStack;
    IfFailRet(Run(pThread, frameLevel, expression, evalStack, output));

    assert(evalStack.size() == 1);

    ToRelease<ICorDebugValue> trValue;
    IfFailRet(GetFrontStackEntryValue(&trValue, nullptr, evalStack, m_evalData, output));

    return ImplicitCast(trValue, pValue, evalStack.front().literal, m_evalData);
}

HRESULT EvalStackMachine::FindPredefinedTypes(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    static const WSTRING strTypeDefDecimal{W("System.Decimal")};
    IfFailRet(trMDImport->FindTypeDefByName(strTypeDefDecimal.c_str(), mdTypeDefNil, &typeDef));
    IfFailRet(pModule->GetClassFromToken(typeDef, &m_evalData.trDecimalClass));

    typeDef = mdTypeDefNil;
    static const WSTRING strTypeDefVoid{W("System.Void")};
    IfFailRet(trMDImport->FindTypeDefByName(strTypeDefVoid.c_str(), mdTypeDefNil, &typeDef));
    IfFailRet(pModule->GetClassFromToken(typeDef, &m_evalData.trVoidClass));

    static const std::vector<std::pair<CorElementType, const WCHAR *>> corElementToValueNameMap{
        {ELEMENT_TYPE_BOOLEAN,  W("System.Boolean")},
        {ELEMENT_TYPE_CHAR,     W("System.Char")},
        {ELEMENT_TYPE_I1,       W("System.SByte")},
        {ELEMENT_TYPE_U1,       W("System.Byte")},
        {ELEMENT_TYPE_I2,       W("System.Int16")},
        {ELEMENT_TYPE_U2,       W("System.UInt16")},
        {ELEMENT_TYPE_I4,       W("System.Int32")},
        {ELEMENT_TYPE_U4,       W("System.UInt32")},
        {ELEMENT_TYPE_I8,       W("System.Int64")},
        {ELEMENT_TYPE_U8,       W("System.UInt64")},
        {ELEMENT_TYPE_R4,       W("System.Single")},
        {ELEMENT_TYPE_R8,       W("System.Double")}
    };

    for (const auto &[elemType, systemTypeName] : corElementToValueNameMap)
    {
        typeDef = mdTypeDefNil;
        IfFailRet(trMDImport->FindTypeDefByName(systemTypeName, mdTypeDefNil, &typeDef));

        assert(m_evalData.trElementToValueClassMap.find(elemType) == m_evalData.trElementToValueClassMap.end());
        m_evalData.trElementToValueClassMap.emplace(elemType, nullptr);
        IfFailRet(pModule->GetClassFromToken(typeDef, &m_evalData.trElementToValueClassMap.at(elemType)));
    }

    return S_OK;
}

} // namespace dncdbg
