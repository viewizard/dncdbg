// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/stackmachine/handler_operators.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/value.h"
#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/valueprint.h"
#include "metadata/helpers.h"
#include "utils/function_ref.h"
#include "utils/hresult.h"
#include <array>
#include <cassert>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dncdbg::EvalStackMachine
{

namespace
{

HRESULT CallUnaryOperator(const std::string &opName, ICorDebugValue *pValue, ICorDebugValue **pResultValue, const EvalData &ed)
{
    HRESULT Status = S_OK;
    std::string metadataTypeName;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetArgData(pValue, metadataTypeName, elemType));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Walkers::WalkMethods(pValue, true,
        [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, uint32_t /*methodGenParamCount*/,
            const Walkers::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (!isStatic || methodArgs.size() != 1 || opName != methodName ||
                elemType != methodArgs.at(0).elemType || metadataTypeName != methodArgs.at(0).metadataTypeName)
            {
                return S_OK; // Return success to continue walking.
            }

            IfFailRet(getFunction(&trFunc));

            return S_CAN_EXIT; // Fast exit from the loop.
        }));

    if (trFunc == nullptr)
    {
        return E_FAIL;
    }

    return EvalExec::CallFunction(ed.pThread, trFunc, nullptr, nullptr, &pValue,
                                  1, ed.specifier, pResultValue);
}

HRESULT CallBinaryOperator(const std::string &opName, ICorDebugValue *pValue, ICorDebugValue *pType1Value,
                           ICorDebugValue *pType2Value, ICorDebugValue **pResultValue, const EvalData &ed)
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
    const auto CallOperator =
        [&](const FunctionRef<HRESULT(std::vector<SigElementType> &)> &cb) -> HRESULT
        {
            ToRelease<ICorDebugFunction> trFunc;
            IfFailRet(Walkers::WalkMethods(pValue, true,
                [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
                    std::vector<SigElementType> &methodArgs, uint32_t /*methodGenParamCount*/,
                    const Walkers::GetFunctionCallback &getFunction) -> HRESULT
                {
                    if (!isStatic || methodArgs.size() != 2 || opName != methodName || FAILED(cb(methodArgs)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    IfFailRet(getFunction(&trFunc));

                    return S_CAN_EXIT; // Fast exit from the loop, since we already found trFunc.
                }));

            if (trFunc == nullptr)
            {
                return E_INVALIDARG;
            }

            std::array<ICorDebugValue *, 2> argsValue{pType1Value, pType2Value};
            return EvalExec::CallFunction(ed.pThread, trFunc, nullptr, nullptr, argsValue.data(),
                                          2, ed.specifier, pResultValue);
        };

    // Try to execute the operator for the exact same type as the provided values.
    if (SUCCEEDED(CallOperator([&](std::vector<SigElementType> &methodArgs) {
            return elemType1 != methodArgs.at(0).elemType || typeName1 != methodArgs.at(0).metadataTypeName ||
                   elemType2 != methodArgs.at(1).elemType || typeName2 != methodArgs.at(1).metadataTypeName
                    ? E_FAIL : S_OK;
        })))
    {
        return S_OK;
    }

    // Try to execute the operator with an implicit cast for the second value.
    // Make sure we don't cast the "base" struct/class value for this case,
    // since "... at least one parameter must have type T...".
    if (elemType == elemType1 && typeName == typeName1 &&
        SUCCEEDED(CallOperator([&](std::vector<SigElementType> &methodArgs) {
            if (elemType1 != methodArgs.at(0).elemType || typeName1 != methodArgs.at(0).metadataTypeName)
            {
                return E_FAIL;
            }

            ToRelease<ICorDebugValue> trResultValue;
            if (FAILED(CallCastOperator("op_Implicit", pType1Value, methodArgs.at(1).elemType, methodArgs.at(1).metadataTypeName,
                                        pType2Value, &trResultValue, ed)) &&
                FAILED(CallCastOperator("op_Implicit", pType2Value, methodArgs.at(1).elemType, methodArgs.at(1).metadataTypeName,
                                        pType2Value, &trResultValue, ed)))
            {
                return E_FAIL;
            }

            IfFailRet(GetRealValueWithType(trResultValue, &trTypeValue));
            // The assignment modifies a local variable captured by reference, and
            // that modified value is used by CallOperator when it calls CallFunction.
            pType2Value = trTypeValue.GetPtr();

            return S_OK;
        })))
    {
        return S_OK;
    }

    // Try to execute the operator with an implicit cast for the first value.
    return CallOperator([&](std::vector<SigElementType> &methodArgs) {
        if (elemType2 != methodArgs.at(1).elemType || typeName2 != methodArgs.at(1).metadataTypeName)
        {
            return E_FAIL;
        }

        ToRelease<ICorDebugValue> trResultValue;
        if (FAILED(CallCastOperator("op_Implicit", pType1Value, methodArgs.at(0).elemType, methodArgs.at(0).metadataTypeName,
                                    pType1Value, &trResultValue, ed)) &&
            FAILED(CallCastOperator("op_Implicit", pType2Value, methodArgs.at(0).elemType, methodArgs.at(0).metadataTypeName,
                                    pType1Value, &trResultValue, ed)))
        {
            return E_FAIL;
        }

        trTypeValue.Free();
        IfFailRet(GetRealValueWithType(trResultValue, &trTypeValue));
        // The assignment modifies a local variable captured by reference, and
        // that modified value is used by CallOperator when it calls CallFunction.
        pType1Value = trTypeValue.GetPtr();

        return S_OK;
    });
}

} // unnamed namespace

HRESULT BinaryOperator(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue2;
    IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trValue2, nullptr, output));
    evalStack.pop_front();
    ToRelease<ICorDebugValue> trRealValue2;
    CorElementType elemType2 = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue2, &trRealValue2, &elemType2));

    ToRelease<ICorDebugValue> trValue1;
    IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trValue1, nullptr, output));
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
        {Parser::SyntaxKind::UnsignedRightShiftExpression, {"op_UnsignedRightShift", ">>>"}},
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
    if (findOpName == opMap.cend())
    {
        return E_FAIL;
    }

    const auto fillErrorOutput = [&]
    {
        std::string typeName1 = "unknown";
        MetadataHelpers::GetFQDisplayTypeName(trRealValue1, typeName1);
        std::string typeName2 = "unknown";
        MetadataHelpers::GetFQDisplayTypeName(trRealValue2, typeName2);
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
        // Try to implicitly cast a struct/class object into a primitive type or string.
        if ((PrimitiveTypes::IsPrimitiveType(elemType2) || elemType2 == ELEMENT_TYPE_STRING) && // The first operand is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
            SUCCEEDED(GetArgData(trRealValue2, typeRetName, elemRetType)) &&
            SUCCEEDED(CallCastOperator("op_Implicit", trRealValue1, elemRetType, typeRetName, trRealValue1,
                                       &trResultValue, ed)))
        {
            trRealValue1.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue1, &elemType1));
            // Fall through to the string and primitive types handling below.
        }
        else if ((PrimitiveTypes::IsPrimitiveType(elemType1) || elemType1 == ELEMENT_TYPE_STRING) && // The second operand is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
                 SUCCEEDED(GetArgData(trRealValue1, typeRetName, elemRetType)) &&
                 SUCCEEDED(CallCastOperator("op_Implicit", trRealValue2, elemRetType, typeRetName, trRealValue2, &trResultValue, ed)))
        {
            trRealValue2.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue2, &elemType2));
            // Fall through to the string and primitive types handling below.
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
        const auto getString = [](ICorDebugValue *pValue, CorElementType elemType, std::string &strValue) -> HRESULT
        {
            HRESULT Status = S_OK;
            BOOL isNull = FALSE;
            ToRelease<ICorDebugValue> trStrValue;
            if (elemType == ELEMENT_TYPE_STRING)
            {
                IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(pValue, &trStrValue, &isNull));
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
            return EvalExec::CreateString(ed.pThread, string1 + string2, &evalStack.front().trValue);
        }
        else if (elemType1 != ELEMENT_TYPE_BOOLEAN &&
                 (opcode.kind == Parser::SyntaxKind::LogicalAndExpression ||
                  opcode.kind == Parser::SyntaxKind::LogicalOrExpression))
        {
            std::string displayTypeName = "unknown";
            MetadataHelpers::GetFQDisplayTypeName(trRealValue1, displayTypeName);
            output = "error: Cannot implicitly convert type '" + displayTypeName + "' to 'bool'";
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

HRESULT UnaryOperator(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trValue, nullptr, output));
    evalStack.front().ResetEntry(EvalStackEntry::ResetLiteralStatus::No);
    ToRelease<ICorDebugValue> trRealValue;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue, &trRealValue, &elemType));

    static const std::unordered_map<Parser::SyntaxKind, std::pair<std::string, std::string>> opMap{
        {Parser::SyntaxKind::LogicalNotExpression, {"op_LogicalNot", "!"}},
        {Parser::SyntaxKind::BitwiseNotExpression, {"op_OnesComplement", "~"}},
        {Parser::SyntaxKind::UnaryPlusExpression, {"op_UnaryPlus", "+"}},
        {Parser::SyntaxKind::UnaryMinusExpression, {"op_UnaryNegation", "-"}}};

    const auto findOpName = opMap.find(opcode.kind);
    if (findOpName == opMap.cend())
    {
        return E_FAIL;
    }

    const auto fillErrorOutput = [&]
    {
        std::string displayTypeName = "unknown";
        MetadataHelpers::GetFQDisplayTypeName(trRealValue, displayTypeName);
        output = "error: Operator '" + findOpName->second.second + "' cannot be applied to operand of type '" + displayTypeName + "'";
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

HRESULT CoalesceExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trRealValueRightOp;
    ToRelease<ICorDebugValue> trRightOpValue;
    CorElementType elemTypeRightOp = ELEMENT_TYPE_MAX;
    IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trRightOpValue, nullptr, output));
    IfFailRet(GetRealValueWithType(trRightOpValue, &trRealValueRightOp, &elemTypeRightOp));
    auto rightOperand = std::move(evalStack.front());
    evalStack.pop_front();

    ToRelease<ICorDebugValue> trRealValueLeftOp;
    ToRelease<ICorDebugValue> trLeftOpValue;
    CorElementType elemTypeLeftOp = ELEMENT_TYPE_MAX;
    IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trLeftOpValue, nullptr, output));
    IfFailRet(GetRealValueWithType(trLeftOpValue, &trRealValueLeftOp, &elemTypeLeftOp));
    std::string typeNameLeft;
    std::string typeNameRight;

    // TODO: implement nullable type support.

    // TODO: add an implementation for `object ?? other`.
    if ((elemTypeRightOp == ELEMENT_TYPE_STRING && elemTypeLeftOp == ELEMENT_TYPE_STRING) ||
        ((elemTypeRightOp == ELEMENT_TYPE_CLASS && elemTypeLeftOp == ELEMENT_TYPE_CLASS) &&
         SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByICorValue(trRealValueLeftOp, typeNameLeft)) &&
         SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByICorValue(trRealValueRightOp, typeNameRight)) &&
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
    // TODO: add handling for parent-child class relationships.
    std::string typeName1;
    std::string typeName2;
    IfFailRet(MetadataHelpers::GetFQDisplayTypeName(trRealValueLeftOp, typeName1));
    IfFailRet(MetadataHelpers::GetFQDisplayTypeName(trRealValueRightOp, typeName2));
    output = "error CS0019: Operator ?? cannot be applied to operands of type '" + typeName1 + "' and '" + typeName2 + "'";
    return E_INVALIDARG;
}

HRESULT SizeOfExpression(const Parser::Opcode &/*opcode*/, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    assert(!evalStack.empty());
    HRESULT Status = S_OK;
    uint32_t size = 0;
    void *szPtr = &size;

    if (evalStack.front().trValue != nullptr)
    {
        // Predefined type.
        CorElementType elType = ELEMENT_TYPE_MAX;
        IfFailRet(evalStack.front().trValue->GetType(&elType));
        if (elType == ELEMENT_TYPE_CLASS)
        {
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(evalStack.front().trValue, &trValue, nullptr));
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

        IfFailRet(GetFrontStackEntryType(evalStack, ed, &trType, output));
        if (trType != nullptr)
        {
            CorElementType elType = ELEMENT_TYPE_MAX;
            IfFailRet(trType->GetType(&elType));
            if (elType == ELEMENT_TYPE_VALUETYPE)
            {
                // User-defined type (structure).
                ToRelease<ICorDebugClass> trClass;

                IfFailRet(trType->GetClass(&trClass));
                IfFailRet(EvalExec::CreateValueType(ed.pThread, trClass, nullptr, &trValueRef));
                IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(trValueRef, &trValue, nullptr));
                IfFailRet(trValue->GetSize(&size));
            }
            else
            {
                return E_INVALIDARG;
            }
        }
        else
        {
            // TODO: other cases.
            return E_NOTIMPL;
        }
    }
    evalStack.front().ResetEntry();
    return PrimitiveTypes::CreateICorValue(ed.pThread, ELEMENT_TYPE_U4, szPtr, &evalStack.front().trValue);
}

} // namespace dncdbg::EvalStackMachine
