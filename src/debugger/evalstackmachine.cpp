// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalstackmachine.h"
#include "debugger/evalhelpers.h"
#include "debugger/evalutils.h"
#include "debugger/evalwaiter.h"
#include "debugger/valueprint.h"
#include "managed/interop.h"
#include "metadata/typeprinter.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <sstream>
#include <vector>
#include <utility>

namespace dncdbg
{

namespace
{
struct FormatF
{
    uint32_t Flags;
};

struct FormatFS
{
    uint32_t Flags;
    BSTR wString;
};

struct FormatFI
{
    uint32_t Flags;
    int32_t Int;
};

struct FormatFIS
{
    uint32_t Flags;
    int32_t Int;
    BSTR wString;
};

struct FormatFIP
{
    uint32_t Flags;
    int32_t Int;
    void *Ptr;
};

// Keep in sync with BasicTypes enum in Evaluation.cs
enum class BasicTypes : uint8_t
{
    TypeBoolean = 1,
    TypeByte,
    TypeSByte,
    TypeChar,
    TypeDouble,
    TypeSingle,
    TypeInt32,
    TypeUInt32,
    TypeInt64,
    TypeUInt64,
    TypeInt16,
    TypeUInt16,
    TypeString
};

// Keep in sync with OperationType enum in Evaluation.cs
enum class OperationType : uint8_t
{
    AddExpression = 1,
    SubtractExpression,
    MultiplyExpression,
    DivideExpression,
    ModuloExpression,
    RightShiftExpression,
    LeftShiftExpression,
    BitwiseNotExpression,
    LogicalAndExpression,
    LogicalOrExpression,
    ExclusiveOrExpression,
    BitwiseAndExpression,
    BitwiseOrExpression,
    LogicalNotExpression,
    EqualsExpression,
    NotEqualsExpression,
    LessThanExpression,
    GreaterThanExpression,
    LessThanOrEqualExpression,
    GreaterThanOrEqualExpression,
    UnaryPlusExpression,
    UnaryMinusExpression
};

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
    // TODO more internal names should be added: $thread, ... (see internal variables supported by MSVS C# debugger)
    static const std::vector<std::pair<std::string, std::string>> internalNamesMap{
        {"$exception", "__INTERNAL_NCDB_EXCEPTION_VARIABLE"}};

    for (const auto &entry : internalNamesMap)
    {
        if (restore)
        {
            ReplaceAllSubstring(expression, entry.second, entry.first);
        }
        else
        {
            ReplaceAllSubstring(expression, entry.first, entry.second);
        }
    }
}

HRESULT CreatePrimitiveValue(ICorDebugThread *pThread, ICorDebugValue **ppValue, CorElementType type, void *ptr)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugEval> trEval;
    IfFailRet(pThread->CreateEval(&trEval));
    IfFailRet(trEval->CreateValue(type, nullptr, ppValue));

    if (ptr == nullptr)
    {
        return S_OK;
    }

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet((*ppValue)->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));
    return trGenValue->SetValue(ptr);
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

    uint32_t cbSize = 0;
    IfFailRet((*ppValue)->GetSize(&cbSize));
    std::vector<uint8_t> valueData(cbSize, 0);

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet((*ppValue)->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));

    IfFailRet(trGenValue->GetValue(static_cast<void *>(valueData.data())));
    valueData.at(0) = 1; // TRUE

    return trGenValue->SetValue(static_cast<void *>(valueData.data()));
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
    IfFailRet(pEvalWaiter->WaitEvalResult(pThread, ppValue, [&](ICorDebugEval *pEval) -> HRESULT {
        // Note, this code execution is protected by EvalWaiter mutex.
        ToRelease<ICorDebugEval2> pEval2;
        IfFailRet(pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&pEval2)));
        IfFailRet(pEval2->NewParameterizedObjectNoConstructor(pValueTypeClass, 0, nullptr));
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

HRESULT GetElementIndex(ICorDebugValue *pInputValue, uint32_t &index)
{
    // `uint32_t &index` - ICorDebugArrayValue::GetElement expect uint32_t indices

    HRESULT Status = S_OK;

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_INVALIDARG;
    }

    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    std::vector<uint8_t> indexValue(cbSize, 0);

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(static_cast<void *>(indexValue.data())));

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&elemType));

    switch (elemType)
    {
    case ELEMENT_TYPE_I1:
    {
        const int8_t tmp = *reinterpret_cast<int8_t *>(indexValue.data());
        if (tmp < 0)
        {
            return E_INVALIDARG;
        }
        index = static_cast<uint32_t>(static_cast<uint8_t>(tmp));
        break;
    }
    case ELEMENT_TYPE_U1:
    {
        index = static_cast<uint32_t>(indexValue.at(0));
        break;
    }
    case ELEMENT_TYPE_I2:
    {
        const int16_t tmp = *reinterpret_cast<int16_t *>(indexValue.data());
        if (tmp < 0)
        {
            return E_INVALIDARG;
        }
        index = static_cast<uint32_t>(static_cast<uint16_t>(tmp));
        break;
    }
    case ELEMENT_TYPE_U2:
    {
        index = static_cast<uint32_t>(*reinterpret_cast<uint16_t *>(indexValue.data()));
        break;
    }
    case ELEMENT_TYPE_I4:
    {
        const int32_t tmp = *reinterpret_cast<int32_t *>(indexValue.data());
        if (tmp < 0)
        {
            return E_INVALIDARG;
        }
        index = static_cast<uint32_t>(tmp);
        break;
    }
    case ELEMENT_TYPE_U4:
    {
        index = *reinterpret_cast<uint32_t *>(indexValue.data());
        break;
    }
    case ELEMENT_TYPE_I8:
    {
        const int64_t tmp = *reinterpret_cast<int64_t *>(indexValue.data());
        if (tmp < 0)
        {
            return E_INVALIDARG;
        }
        index = static_cast<uint32_t>(tmp);
        break;
    }
    case ELEMENT_TYPE_U8:
    {
        index = static_cast<uint32_t>(*reinterpret_cast<uint64_t *>(indexValue.data()));
        break;
    }
    default:
        return E_INVALIDARG;
    }

    return S_OK;
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
    IfFailRet(Evaluator::WalkMethods(pValue,
        [&](bool is_static, const std::string &methodName, Evaluator::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (!is_static || methodArgs.size() != 1 || opName != methodName ||
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
    IfFailRet(Evaluator::WalkMethods(pValue,
        [&](bool is_static, const std::string &methodName, Evaluator::ReturnElementType &methodRet,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (!is_static || methodArgs.size() != 1 || opName != methodName ||
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

template <typename T1, typename T2>
HRESULT ImplicitCastElemType(ICorDebugValue *pValue1, ICorDebugValue *pValue2, bool testRange)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugGenericValue> trGenericValue1;
    IfFailRet(pValue1->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue1)));
    T1 value1 = 0;
    IfFailRet(trGenericValue1->GetValue(&value1));

    if (testRange &&
        ((value1 < 0 && (std::numeric_limits<T2>::min() == 0 || value1 - std::numeric_limits<T2>::min() < 0)) ||
         (value1 > 0 && std::numeric_limits<T2>::max() - value1 < 0)))
    {
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue2;
    IfFailRet(pValue2->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue2)));
    T2 value2 = static_cast<T2>(value1); // NOLINT(cert-str34-c,bugprone-signed-char-misuse)
    return trGenericValue2->SetValue(&value2);
}

using ImplicitCastMap_t = std::array<std::array<std::function<HRESULT(ICorDebugValue *, ICorDebugValue *, bool)>, ELEMENT_TYPE_MAX>, ELEMENT_TYPE_MAX>;

ImplicitCastMap_t InitImplicitCastMap()
{
    ImplicitCastMap_t implicitCastMap;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_U2) = ImplicitCastElemType<uint16_t, uint16_t>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_I4) = ImplicitCastElemType<uint16_t, int32_t>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_U4) = ImplicitCastElemType<uint16_t, uint32_t>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<uint16_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_U8) = ImplicitCastElemType<uint16_t, uint64_t>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<uint16_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_CHAR).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<uint16_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_I1).at(ELEMENT_TYPE_I2) = ImplicitCastElemType<int8_t, int16_t>;
    implicitCastMap.at(ELEMENT_TYPE_I1).at(ELEMENT_TYPE_I4) = ImplicitCastElemType<int8_t, int32_t>;
    implicitCastMap.at(ELEMENT_TYPE_I1).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<int8_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_I1).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<int8_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_I1).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<int8_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_I2) = ImplicitCastElemType<uint8_t, int16_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_U2) = ImplicitCastElemType<uint8_t, uint16_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_I4) = ImplicitCastElemType<uint8_t, int32_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_U4) = ImplicitCastElemType<uint8_t, uint32_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<uint8_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_U8) = ImplicitCastElemType<uint8_t, uint64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<uint8_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_U1).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<uint8_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_I2).at(ELEMENT_TYPE_I4) = ImplicitCastElemType<int16_t, int32_t>;
    implicitCastMap.at(ELEMENT_TYPE_I2).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<int16_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_I2).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<int16_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_I2).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<int16_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_I4) = ImplicitCastElemType<uint16_t, int32_t>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_U4) = ImplicitCastElemType<uint16_t, uint32_t>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<uint16_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_U8) = ImplicitCastElemType<uint16_t, uint64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<uint16_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_U2).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<uint16_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<int32_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<int32_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<int32_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_U4).at(ELEMENT_TYPE_I8) = ImplicitCastElemType<uint32_t, int64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U4).at(ELEMENT_TYPE_U8) = ImplicitCastElemType<uint32_t, uint64_t>;
    implicitCastMap.at(ELEMENT_TYPE_U4).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<uint32_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_U4).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<uint32_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_I8).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<int64_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_I8).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<int64_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_U8).at(ELEMENT_TYPE_R4) = ImplicitCastElemType<uint64_t, float>;
    implicitCastMap.at(ELEMENT_TYPE_U8).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<uint64_t, double>;
    implicitCastMap.at(ELEMENT_TYPE_R4).at(ELEMENT_TYPE_R8) = ImplicitCastElemType<float, double>;

    return implicitCastMap;
}

ImplicitCastMap_t InitImplicitCastLiteralMap()
{
    ImplicitCastMap_t implicitCastLiteralMap;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_I1) = ImplicitCastElemType<int32_t, int8_t>;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_U1) = ImplicitCastElemType<int32_t, uint8_t>;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_I2) = ImplicitCastElemType<int32_t, int16_t>;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_U2) = ImplicitCastElemType<int32_t, uint16_t>;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_U4) = ImplicitCastElemType<int32_t, uint32_t>;
    implicitCastLiteralMap.at(ELEMENT_TYPE_I4).at(ELEMENT_TYPE_U8) = ImplicitCastElemType<int32_t, uint64_t>;

    return implicitCastLiteralMap;
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
    if (elemTypeDst == ELEMENT_TYPE_BOOLEAN || elemTypeDst == ELEMENT_TYPE_CHAR || elemTypeDst == ELEMENT_TYPE_I1 ||
        elemTypeDst == ELEMENT_TYPE_U1 || elemTypeDst == ELEMENT_TYPE_I2 || elemTypeDst == ELEMENT_TYPE_U2 ||
        elemTypeDst == ELEMENT_TYPE_U4 || elemTypeDst == ELEMENT_TYPE_I4 || elemTypeDst == ELEMENT_TYPE_I8 ||
        elemTypeDst == ELEMENT_TYPE_U8 || elemTypeDst == ELEMENT_TYPE_R4 || elemTypeDst == ELEMENT_TYPE_R8 ||
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
                          elemType1 == ELEMENT_TYPE_CLASS || elemType2 == ELEMENT_TYPE_CLASS))
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

    static ImplicitCastMap_t implicitCastMap = InitImplicitCastMap();
    static ImplicitCastMap_t implicitCastLiteralMap = InitImplicitCastLiteralMap();

    if (srcLiteral && implicitCastLiteralMap.at(elemType1).at(elemType2) != nullptr)
    {
        return implicitCastLiteralMap.at(elemType1).at(elemType2)(trRealValue1, trRealValue2, true);
    }

    if (implicitCastMap.at(elemType1).at(elemType2) != nullptr)
    {
        return implicitCastMap.at(elemType1).at(elemType2)(trRealValue1, trRealValue2, false);
    }

    return E_INVALIDARG;
}

HRESULT GetOperandDataTypeByValue(ICorDebugValue *pValue, CorElementType elemType, void **resultData, int32_t &resultType)
{
    HRESULT Status = S_OK;

    if (elemType == ELEMENT_TYPE_STRING)
    {
        resultType = static_cast<int32_t>(BasicTypes::TypeString);
        ToRelease<ICorDebugValue> trValue;
        BOOL isNull = FALSE;
        IfFailRet(DereferenceAndUnboxValue(pValue, &trValue, &isNull));
        *resultData = nullptr;
        if (isNull == FALSE)
        {
            std::string String;
            IfFailRet(PrintStringValue(trValue, String));
            *resultData = Interop::AllocString(String);
        }
        return S_OK;
    }

    static std::unordered_map<CorElementType, BasicTypes> basicTypesMap{
        {ELEMENT_TYPE_BOOLEAN, BasicTypes::TypeBoolean}, {ELEMENT_TYPE_U1, BasicTypes::TypeByte},
        {ELEMENT_TYPE_I1, BasicTypes::TypeSByte},        {ELEMENT_TYPE_CHAR, BasicTypes::TypeChar},
        {ELEMENT_TYPE_R8, BasicTypes::TypeDouble},       {ELEMENT_TYPE_R4, BasicTypes::TypeSingle},
        {ELEMENT_TYPE_I4, BasicTypes::TypeInt32},        {ELEMENT_TYPE_U4, BasicTypes::TypeUInt32},
        {ELEMENT_TYPE_I8, BasicTypes::TypeInt64},        {ELEMENT_TYPE_U8, BasicTypes::TypeUInt64},
        {ELEMENT_TYPE_I2, BasicTypes::TypeInt16},        {ELEMENT_TYPE_U2, BasicTypes::TypeUInt16}};

    auto findType = basicTypesMap.find(elemType);
    if (findType == basicTypesMap.end())
    {
        return E_FAIL;
    }
    resultType = static_cast<int32_t>(findType->second);

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));
    return trGenValue->GetValue(*resultData);
}

HRESULT GetValueByOperandDataType(void *valueData, BasicTypes valueType, ICorDebugValue **ppValue, EvalData &ed)
{
    if (valueType == BasicTypes::TypeString)
    {
        const std::string String = to_utf8(static_cast<WCHAR *>(valueData));
        return ed.pEvalHelpers->CreateString(ed.pThread, String, ppValue);
    }

    static std::unordered_map<BasicTypes, CorElementType> basicTypesMap{
        {BasicTypes::TypeBoolean, ELEMENT_TYPE_BOOLEAN}, {BasicTypes::TypeByte, ELEMENT_TYPE_U1},
        {BasicTypes::TypeSByte, ELEMENT_TYPE_I1},        {BasicTypes::TypeChar, ELEMENT_TYPE_CHAR},
        {BasicTypes::TypeDouble, ELEMENT_TYPE_R8},       {BasicTypes::TypeSingle, ELEMENT_TYPE_R4},
        {BasicTypes::TypeInt32, ELEMENT_TYPE_I4},        {BasicTypes::TypeUInt32, ELEMENT_TYPE_U4},
        {BasicTypes::TypeInt64, ELEMENT_TYPE_I8},        {BasicTypes::TypeUInt64, ELEMENT_TYPE_U8},
        {BasicTypes::TypeInt16, ELEMENT_TYPE_I2},        {BasicTypes::TypeUInt16, ELEMENT_TYPE_U2}};

    auto findType = basicTypesMap.find(valueType);
    if (findType == basicTypesMap.end())
    {
        return E_FAIL;
    }

    return CreatePrimitiveValue(ed.pThread, ppValue, findType->second, valueData);
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
            IfFailRet(Evaluator::WalkMethods(pValue,
                [&](bool is_static, const std::string &methodName, Evaluator::ReturnElementType &,
                    std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
                {
                    if (!is_static || methodArgs.size() != 2 || opName != methodName || FAILED(cb(methodArgs)))
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
        pType1Value = trTypeValue.GetPtr();

        return S_OK;
    });
}

bool SupportedByCalculationType(CorElementType elemType)
{
    static std::unordered_set<CorElementType> supportedElementTypes{
        ELEMENT_TYPE_BOOLEAN, ELEMENT_TYPE_U1, ELEMENT_TYPE_I1,    ELEMENT_TYPE_CHAR, ELEMENT_TYPE_R8,
        ELEMENT_TYPE_R4,      ELEMENT_TYPE_I4, ELEMENT_TYPE_U4,    ELEMENT_TYPE_I8,   ELEMENT_TYPE_U8,
        ELEMENT_TYPE_I2,      ELEMENT_TYPE_U2, ELEMENT_TYPE_STRING};

    return supportedElementTypes.find(elemType) != supportedElementTypes.end();
}

HRESULT CalculateTwoOperands(OperationType opType, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
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

    if (elemType1 == ELEMENT_TYPE_VALUETYPE || elemType2 == ELEMENT_TYPE_VALUETYPE ||
        elemType1 == ELEMENT_TYPE_CLASS || elemType2 == ELEMENT_TYPE_CLASS)
    {
        static std::unordered_map<OperationType, std::pair<std::string, std::string>> opMap{
            {OperationType::AddExpression, {"op_Addition", "+"}},
            {OperationType::SubtractExpression, {"op_Subtraction", "-"}},
            {OperationType::MultiplyExpression, {"op_Multiply", "*"}},
            {OperationType::DivideExpression, {"op_Division", "/"}},
            {OperationType::ModuloExpression, {"op_Modulus", "%"}},
            {OperationType::RightShiftExpression, {"op_RightShift", ">>"}},
            {OperationType::LeftShiftExpression, {"op_LeftShift", "<<"}},
            {OperationType::LogicalAndExpression, {"op_LogicalAnd", "&&"}},
            {OperationType::LogicalOrExpression, {"op_LogicalOr", "||"}},
            {OperationType::ExclusiveOrExpression, {"op_ExclusiveOr", "^"}},
            {OperationType::BitwiseAndExpression, {"op_BitwiseAnd", "&"}},
            {OperationType::BitwiseOrExpression, {"op_BitwiseOr", "|"}},
            {OperationType::EqualsExpression, {"op_Equality", "=="}},
            {OperationType::NotEqualsExpression, {"op_Inequality", "!="}},
            {OperationType::LessThanExpression, {"op_LessThan", "<"}},
            {OperationType::GreaterThanExpression, {"op_GreaterThan", ">"}},
            {OperationType::LessThanOrEqualExpression, {"op_LessThanOrEqual", "<="}},
            {OperationType::GreaterThanOrEqualExpression, {"op_GreaterThanOrEqual", ">="}}};

        auto findOpName = opMap.find(opType);
        if (findOpName == opMap.end())
        {
            return E_FAIL;
        }

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
        // Try to implicitly cast struct/class object into build-in type supported by CalculationDelegate().
        if (SupportedByCalculationType(elemType2) && // First is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
            SUCCEEDED(GetArgData(trRealValue2, typeRetName, elemRetType)) &&
            SUCCEEDED(CallCastOperator("op_Implicit", trRealValue1, elemRetType, typeRetName, trRealValue1,
                                       &trResultValue, ed)))
        {
            trRealValue1.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue1, &elemType1));
            // goto CalculationDelegate() related routine (see code below this 'if' statement scope)
        }
        else if (SupportedByCalculationType(elemType1) && // Second is ELEMENT_TYPE_VALUETYPE or ELEMENT_TYPE_CLASS
                 SUCCEEDED(GetArgData(trRealValue1, typeRetName, elemRetType)) &&
                 SUCCEEDED(CallCastOperator("op_Implicit", trRealValue2, elemRetType, typeRetName, trRealValue2, &trResultValue, ed)))
        {
            trRealValue2.Free();
            IfFailRet(GetRealValueWithType(trResultValue, &trRealValue2, &elemType2));
            // goto CalculationDelegate() related routine (see code below this 'if' statement scope)
        }
        else
        {
            std::string typeName1;
            IfFailRet(TypePrinter::GetTypeOfValue(trRealValue1, typeName1));
            std::string typeName2;
            IfFailRet(TypePrinter::GetTypeOfValue(trRealValue2, typeName2));
            output = "error CS0019: Operator '" + findOpName->second.second +
                     "' cannot be applied to operands of type '" + typeName1 + "' and '" + typeName2 + "'";
            return E_INVALIDARG;
        }
    }
    else if (!SupportedByCalculationType(elemType1) || !SupportedByCalculationType(elemType2))
    {
        return E_INVALIDARG;
    }

    int64_t valueDataHolder1 = 0;
    void *valueData1 = &valueDataHolder1;
    int32_t valueType1 = 0;
    int64_t valueDataHolder2 = 0;
    void *valueData2 = &valueDataHolder2;
    int32_t valueType2 = 0;
    void *resultData = nullptr;
    int32_t resultType = 0;
    if (SUCCEEDED(Status = GetOperandDataTypeByValue(trRealValue1, elemType1, &valueData1, valueType1)) &&
        SUCCEEDED(Status = GetOperandDataTypeByValue(trRealValue2, elemType2, &valueData2, valueType2)) &&
        SUCCEEDED(Status = Interop::Calculation(valueData1, valueType1, valueData2, valueType2,
                                                static_cast<int32_t>(opType), resultType, &resultData, output)))
    {
        Status = GetValueByOperandDataType(resultData, static_cast<BasicTypes>(resultType), &evalStack.front().trValue, ed);
        if (resultType == static_cast<int32_t>(BasicTypes::TypeString))
        {
            Interop::SysFreeString(reinterpret_cast<BSTR>(resultData));
        }
        else
        {
            Interop::CoTaskMemFree(resultData);
        }
    }

    if (valueType1 == static_cast<int32_t>(BasicTypes::TypeString) && (valueData1 != nullptr))
    {
        Interop::SysFreeString(reinterpret_cast<BSTR>(valueData1));
    }

    if (valueType2 == static_cast<int32_t>(BasicTypes::TypeString) && (valueData2 != nullptr))
    {
        Interop::SysFreeString(reinterpret_cast<BSTR>(valueData2));
    }

    return Status;
}

HRESULT CalculateOneOperand(OperationType opType, std::list<EvalStackEntry> &evalStack, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(GetFrontStackEntryValue(&trValue, nullptr, evalStack, ed, output));
    evalStack.front().ResetEntry(EvalStackEntry::ResetLiteralStatus::No);
    ToRelease<ICorDebugValue> trRealValue;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(GetRealValueWithType(trValue, &trRealValue, &elemType));

    if (elemType == ELEMENT_TYPE_VALUETYPE || elemType == ELEMENT_TYPE_CLASS)
    {
        static std::unordered_map<OperationType, std::pair<std::string, std::string>> opMap{
            {OperationType::LogicalNotExpression, {"op_LogicalNot", "!"}},
            {OperationType::BitwiseNotExpression, {"op_OnesComplement", "~"}},
            {OperationType::UnaryPlusExpression, {"op_UnaryPlus", "+"}},
            {OperationType::UnaryMinusExpression, {"op_UnaryNegation", "-"}}};

        auto findOpName = opMap.find(opType);
        if (findOpName == opMap.end())
        {
            return E_FAIL;
        }

        if (SUCCEEDED(CallUnaryOperator(findOpName->second.first, trRealValue, &evalStack.front().trValue, ed)))
        {
            return S_OK;
        }
        else
        {
            std::string typeName;
            IfFailRet(TypePrinter::GetTypeOfValue(trRealValue, typeName));
            output = "error CS0023: Operator '" + findOpName->second.second + "' cannot be applied to operand of type '" + typeName + "'";
            return E_INVALIDARG;
        }
    }
    else if (!SupportedByCalculationType(elemType))
    {
        return E_INVALIDARG;
    }

    int64_t valueDataHolder1 = 0;
    void *valueData1 = &valueDataHolder1;
    int32_t valueType1 = 0;
    // Note, we need fake second operand for delegate.
    int64_t fakeValueData2 = 0;
    void *resultData = nullptr;
    int32_t resultType = 0;
    if (SUCCEEDED(Status = GetOperandDataTypeByValue(trRealValue, elemType, &valueData1, valueType1)) &&
        SUCCEEDED(Status = Interop::Calculation(valueData1, valueType1, &fakeValueData2, static_cast<int32_t>(BasicTypes::TypeInt64),
                                                static_cast<int32_t>(opType), resultType, &resultData, output)))
    {
        Status = GetValueByOperandDataType(resultData, static_cast<BasicTypes>(resultType), &evalStack.front().trValue, ed);
        if (resultType == static_cast<int32_t>(BasicTypes::TypeString))
        {
            Interop::SysFreeString(reinterpret_cast<BSTR>(resultData));
        }
        else
        {
            Interop::CoTaskMemFree(resultData);
        }
    }

    if (valueType1 == static_cast<int32_t>(BasicTypes::TypeString) && (valueData1 != nullptr))
    {
        Interop::SysFreeString(reinterpret_cast<BSTR>(valueData1));
    }

    return Status;
}

HRESULT IdentifierName(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &/*output*/, EvalData &/*ed*/)
{
    std::string String = to_utf8(static_cast<FormatFS *>(pArguments)->wString);
    ReplaceInternalNames(String, true);

    evalStack.emplace_front();
    evalStack.front().identifiers.emplace_back(std::move(String));
    evalStack.front().editable = true;
    return S_OK;
}

HRESULT GenericName(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &output, EvalData &ed)
{
    HRESULT Status = S_OK;
    const int32_t Int = static_cast<FormatFIS *>(pArguments)->Int;
    std::string String = to_utf8(static_cast<FormatFIS *>(pArguments)->wString);
    std::vector<ToRelease<ICorDebugType>> trGenericValues;
    std::string generics = ">";
    trGenericValues.reserve(Int);
    for (int i = 0; i < Int; i++)
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
    String += "<" + generics;
    evalStack.emplace_front();
    evalStack.front().identifiers.emplace_back(std::move(String));
    evalStack.front().trGenericTypeCache = std::move(trGenericValues);
    evalStack.front().editable = true;
    return S_OK;
}

HRESULT InvocationExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &output, EvalData &ed)
{
    const int32_t Int = static_cast<FormatFI *>(pArguments)->Int;

    if (Int < 0)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    bool idsEmpty = false;
    bool isInstance = true;
    std::vector<ToRelease<ICorDebugValue>> trArgs(Int);
    for (int32_t i = Int - 1; i >= 0; i--)
    {
        IfFailRet(GetFrontStackEntryValue(&trArgs.at(i), nullptr, evalStack, ed, output));
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

    std::vector<SigElementType> funcArgs(Int);
    for (int32_t i = 0; i < Int; ++i)
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
    IfFailRet(Evaluator::WalkMethods(trType, &trResultType,
        [&](bool is_static, const std::string &methodName, Evaluator::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, const Evaluator::GetFunctionCallback &getFunction) -> HRESULT
        {
            if ((searchStatic && !is_static) || (!searchStatic && is_static && !idsEmpty) ||
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
            isInstance = !is_static;

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

    if (trResultType != nullptr)
    {
        trType = trResultType.Detach();
    }

    const uint32_t realArgsCount = Int + (isInstance ? 1 : 0);
    std::vector<ICorDebugValue *> pValueArgs;
    pValueArgs.reserve(realArgsCount);

    // Place instance value ("this") if extension or not static method
    if (isInstance)
    {
        pValueArgs.emplace_back(trValue.GetPtr());
    }

    // Add arguments values
    for (int32_t i = 0; i < Int; i++)
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

    // CORDBG_S_FUNC_EVAL_HAS_NO_RESULT: Some Func evals will lack a return value, such as those whose return type is
    // void.
    if (Status == CORDBG_S_FUNC_EVAL_HAS_NO_RESULT)
    {
        // We can't create ELEMENT_TYPE_VOID, so, we are forced to use System.Void instead.
        IfFailRet(CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trVoidClass, &evalStack.front().trValue, nullptr));
    }

    return Status;
}

HRESULT ObjectCreationExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatFI *>(pArguments)->Flags;
    // TODO int32_t Int = static_cast<FormatFI *>(pArguments)->Int;
    return E_NOTIMPL;
}

HRESULT ElementAccessExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &output, EvalData &ed)
{
    const int32_t Int = static_cast<FormatFI *>(pArguments)->Int;
    HRESULT Status = S_OK;

    std::vector<ToRelease<ICorDebugValue>> trIndexValues(Int);

    for (int32_t i = Int - 1; i >= 0; i--)
    {
        IfFailRet(GetFrontStackEntryValue(&trIndexValues.at(i), nullptr, evalStack, ed, output));
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
        for (int32_t i = Int - 1; i >= 0; i--)
        {
            uint32_t result_index = 0;
            // TODO implicitly convert ICorValue to int, if type is not int
            // currently GetElementIndex() works with integer types only
            IfFailRet(GetElementIndex(trIndexValues.at(i), result_index));
            indexes.insert(indexes.begin(), result_index);
        }
        evalStack.front().trValue.Free();
        evalStack.front().identifiers.clear();
        evalStack.front().setterData = std::move(setterData);
        Status = dncdbg::Evaluator::GetElement(trObjectValue, indexes, &evalStack.front().trValue);
    }
    else
    {
        std::vector<SigElementType> funcArgs(Int);
        for (int32_t i = 0; i < Int; ++i)
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
        IfFailRet(Evaluator::WalkMethods(trObjectValue,
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
        trValueArgs.reserve(Int + 1);

        trValueArgs.emplace_back(trObjectValue.GetPtr());

        for (int32_t i = 0; i < Int; i++)
        {
            trValueArgs.emplace_back(trIndexValues.at(i).GetPtr());
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trObjectValue->QueryInterface(IID_ICorDebugValue2,reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));

        Status = ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, trType.GetPtr(), nullptr, trValueArgs.data(),
                                               Int + 1, &evalStack.front().trValue);
    }
    return Status;
}

HRESULT ElementBindingExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &output, EvalData &ed)
{
    const int32_t Int = static_cast<FormatFI *>(pArguments)->Int;
    HRESULT Status = S_OK;

    std::vector<ToRelease<ICorDebugValue>> trIndexValues(Int);

    for (int32_t i = Int - 1; i >= 0; i--)
    {
        IfFailRet(GetFrontStackEntryValue(&trIndexValues.at(i), nullptr, evalStack, ed, output));
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
        for (int32_t i = Int - 1; i >= 0; i--)
        {
            uint32_t result_index = 0;
            // TODO implicitly convert ICorValue to int, if type is not int
            // currently GetElementIndex() works with integer types only
            IfFailRet(GetElementIndex(trIndexValues.at(i), result_index));
            indexes.insert(indexes.begin(), result_index);
        }
        evalStack.front().trValue.Free();
        evalStack.front().identifiers.clear();
        evalStack.front().setterData = std::move(setterData);
        Status = dncdbg::Evaluator::GetElement(trObjectValue, indexes, &evalStack.front().trValue);
    }
    else
    {
        std::vector<SigElementType> funcArgs(Int);
        for (int32_t i = 0; i < Int; ++i)
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
        IfFailRet(Evaluator::WalkMethods(trObjectValue,
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
        trValueArgs.reserve(Int + 1);

        trValueArgs.emplace_back(trObjectValue.GetPtr());

        for (int32_t i = 0; i < Int; i++)
        {
            trValueArgs.emplace_back(trIndexValues.at(i).GetPtr());
        }

        ToRelease<ICorDebugValue2> trValue2;
        IfFailRet(trObjectValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
        ToRelease<ICorDebugType> trType;
        IfFailRet(trValue2->GetExactType(&trType));

        Status = ed.pEvalHelpers->EvalFunction(ed.pThread, trFunc, trType.GetPtr(), nullptr, trValueArgs.data(),
                                               Int + 1, &evalStack.front().trValue);
    }
    return Status;
}

HRESULT NumericLiteralExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &/*output*/, EvalData &ed)
{
    const int32_t Int = static_cast<FormatFIP *>(pArguments)->Int;
    void *Ptr = static_cast<FormatFIP *>(pArguments)->Ptr;

    // StackMachine type to CorElementType map.
    static constexpr std::array<CorElementType, 15> BasicTypesAlias{
        ELEMENT_TYPE_MAX,       // Boolean - TrueLiteralExpression or FalseLiteralExpression
        ELEMENT_TYPE_MAX,       // Byte - no literal suffix for byte
        ELEMENT_TYPE_MAX,       // Char - CharacterLiteralExpression
        ELEMENT_TYPE_VALUETYPE, // Decimal
        ELEMENT_TYPE_R8,
        ELEMENT_TYPE_R4,
        ELEMENT_TYPE_I4,
        ELEMENT_TYPE_I8,
        ELEMENT_TYPE_MAX, // Object
        ELEMENT_TYPE_MAX, // SByte - no literal suffix for sbyte
        ELEMENT_TYPE_MAX, // Short - no literal suffix for short
        ELEMENT_TYPE_MAX, // String - StringLiteralExpression
        ELEMENT_TYPE_MAX, // UShort - no literal suffix for ushort
        ELEMENT_TYPE_U4,
        ELEMENT_TYPE_U8};

    evalStack.emplace_front();
    evalStack.front().literal = true;
    if (BasicTypesAlias.at(Int) == ELEMENT_TYPE_VALUETYPE)
    {
        return CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trDecimalClass, &evalStack.front().trValue, Ptr);
    }
    else
    {
        return CreatePrimitiveValue(ed.pThread, &evalStack.front().trValue, BasicTypesAlias.at(Int), Ptr);
    }
}

HRESULT StringLiteralExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &/*output*/, EvalData &ed)
{
    std::string String = to_utf8(static_cast<FormatFS *>(pArguments)->wString);
    ReplaceInternalNames(String, true);
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return ed.pEvalHelpers->CreateString(ed.pThread, String, &evalStack.front().trValue);
}

HRESULT CharacterLiteralExpression(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &/*output*/, EvalData &ed)
{
    void *Ptr = static_cast<FormatFIP *>(pArguments)->Ptr;
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreatePrimitiveValue(ed.pThread, &evalStack.front().trValue, ELEMENT_TYPE_CHAR, Ptr);
}

HRESULT PredefinedType(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &/*output*/, EvalData &ed)
{
    static constexpr std::array<CorElementType, 15> BasicTypesAlias{
        ELEMENT_TYPE_BOOLEAN,   // Boolean
        ELEMENT_TYPE_U1,        // Byte
        ELEMENT_TYPE_CHAR,      // Char
        ELEMENT_TYPE_VALUETYPE, // Decimal
        ELEMENT_TYPE_R8,        // Double
        ELEMENT_TYPE_R4,        // Float
        ELEMENT_TYPE_I4,        // Int
        ELEMENT_TYPE_I8,        // Long
        ELEMENT_TYPE_MAX,       // Object
        ELEMENT_TYPE_I1,        // SByte
        ELEMENT_TYPE_I2,        // Short
        ELEMENT_TYPE_STRING,    // String
        ELEMENT_TYPE_U2,        // UShort
        ELEMENT_TYPE_U4,        // UInt
        ELEMENT_TYPE_U8         // ULong
    };

    // TODO uint32_t Flags = static_cast<FormatFI *>(pArguments)->Flags;
    const int32_t Int = static_cast<FormatFI *>(pArguments)->Int;
    const std::string String;

    evalStack.emplace_front();

    if (BasicTypesAlias.at(Int) == ELEMENT_TYPE_VALUETYPE)
    {
        return CreateValueType(ed.pEvalWaiter, ed.pThread, ed.trDecimalClass, &evalStack.front().trValue, nullptr);
    }
    else if (BasicTypesAlias.at(Int) == ELEMENT_TYPE_STRING)
    {
        return ed.pEvalHelpers->CreateString(ed.pThread, String, &evalStack.front().trValue);
    }
    else
    {
        return CreatePrimitiveValue(ed.pThread, &evalStack.front().trValue, BasicTypesAlias.at(Int), nullptr);
    }
}

HRESULT AliasQualifiedName(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT MemberBindingExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
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

HRESULT ConditionalExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT SimpleMemberAccessExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    assert(evalStack.size() > 1);
    assert(!evalStack.front().trValue);              // Should be unresolved identifier only front element.
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

HRESULT QualifiedName(std::list<EvalStackEntry> &evalStack, void *pArguments, std::string &output, EvalData &ed)
{
    return SimpleMemberAccessExpression(evalStack, pArguments, output, ed);
}

HRESULT PointerMemberAccessExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT CastExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT AsExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT AddExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::AddExpression, evalStack, output, ed);
}

HRESULT MultiplyExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::MultiplyExpression, evalStack, output, ed);
}

HRESULT SubtractExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::SubtractExpression, evalStack, output, ed);
}

HRESULT DivideExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::DivideExpression, evalStack, output, ed);
}

HRESULT ModuloExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::ModuloExpression, evalStack, output, ed);
}

HRESULT LeftShiftExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::LeftShiftExpression, evalStack, output, ed);
}

HRESULT RightShiftExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::RightShiftExpression, evalStack, output, ed);
}

HRESULT BitwiseAndExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::BitwiseAndExpression, evalStack, output, ed);
}

HRESULT BitwiseOrExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::BitwiseOrExpression, evalStack, output, ed);
}

HRESULT ExclusiveOrExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::ExclusiveOrExpression, evalStack, output, ed);
}

HRESULT LogicalAndExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::LogicalAndExpression, evalStack, output, ed);
}

HRESULT LogicalOrExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::LogicalOrExpression, evalStack, output, ed);
}

HRESULT EqualsExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::EqualsExpression, evalStack, output, ed);
}

HRESULT NotEqualsExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::NotEqualsExpression, evalStack, output, ed);
}

HRESULT GreaterThanExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::GreaterThanExpression, evalStack, output, ed);
}

HRESULT LessThanExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::LessThanExpression, evalStack, output, ed);
}

HRESULT GreaterThanOrEqualExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::GreaterThanOrEqualExpression, evalStack, output, ed);
}

HRESULT LessThanOrEqualExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateTwoOperands(OperationType::LessThanOrEqualExpression, evalStack, output, ed);
}

HRESULT IsExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT UnaryPlusExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateOneOperand(OperationType::UnaryPlusExpression, evalStack, output, ed);
}

HRESULT UnaryMinusExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateOneOperand(OperationType::UnaryMinusExpression, evalStack, output, ed);
}

HRESULT LogicalNotExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateOneOperand(OperationType::LogicalNotExpression, evalStack, output, ed);
}

HRESULT BitwiseNotExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
{
    return CalculateOneOperand(OperationType::BitwiseNotExpression, evalStack, output, ed);
}

HRESULT TrueLiteralExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateBooleanValue(ed.pThread, &evalStack.front().trValue, true);
}

HRESULT FalseLiteralExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateBooleanValue(ed.pThread, &evalStack.front().trValue, false);
}

HRESULT NullLiteralExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &/*output*/, EvalData &ed)
{
    evalStack.emplace_front();
    evalStack.front().literal = true;
    return CreateNullValue(ed.pThread, &evalStack.front().trValue);
}

HRESULT PreIncrementExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT PostIncrementExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT PreDecrementExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT PostDecrementExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT SizeOfExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
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
    return CreatePrimitiveValue(ed.pThread, &evalStack.front().trValue, ELEMENT_TYPE_U4, szPtr);
}

HRESULT TypeOfExpression(std::list<EvalStackEntry> &/*evalStack*/, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
{
    // TODO uint32_t Flags = static_cast<FormatF *>(pArguments)->Flags;
    return E_NOTIMPL;
}

HRESULT CoalesceExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &output, EvalData &ed)
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
    std::string typeNameRigth;

    // TODO add implementation for object type ?? other
    if ((elemTypeRightOp == ELEMENT_TYPE_STRING && elemTypeLeftOp == ELEMENT_TYPE_STRING) ||
        ((elemTypeRightOp == ELEMENT_TYPE_CLASS && elemTypeLeftOp == ELEMENT_TYPE_CLASS) &&
         SUCCEEDED(TypePrinter::NameForTypeByValue(trRealValueLeftOp, typeNameLeft)) &&
         SUCCEEDED(TypePrinter::NameForTypeByValue(trRealValueRightOp, typeNameRigth)) &&
         typeNameLeft == typeNameRigth))
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
    // TODO add proccesing for parent-child class relationship
    std::string typeName1;
    std::string typeName2;
    IfFailRet(TypePrinter::GetTypeOfValue(trRealValueLeftOp, typeName1));
    IfFailRet(TypePrinter::GetTypeOfValue(trRealValueRightOp, typeName2));
    output = "error CS0019: Operator ?? cannot be applied to operands of type '" + typeName1 + "' and '" + typeName2 + "'";
    return E_INVALIDARG;
}

HRESULT ThisExpression(std::list<EvalStackEntry> &evalStack, void */*pArguments*/, std::string &/*output*/, EvalData &/*ed*/)
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
    static const std::vector<std::function<HRESULT(std::list<EvalStackEntry> &, void *, std::string &, EvalData &)>> CommandImplementation = {
        IdentifierName,
        GenericName,
        InvocationExpression,
        ObjectCreationExpression,
        ElementAccessExpression,
        ElementBindingExpression,
        NumericLiteralExpression,
        StringLiteralExpression,
        CharacterLiteralExpression,
        PredefinedType,
        QualifiedName,
        AliasQualifiedName,
        MemberBindingExpression,
        ConditionalExpression,
        SimpleMemberAccessExpression,
        PointerMemberAccessExpression,
        CastExpression,
        AsExpression,
        AddExpression,
        MultiplyExpression,
        SubtractExpression,
        DivideExpression,
        ModuloExpression,
        LeftShiftExpression,
        RightShiftExpression,
        BitwiseAndExpression,
        BitwiseOrExpression,
        ExclusiveOrExpression,
        LogicalAndExpression,
        LogicalOrExpression,
        EqualsExpression,
        NotEqualsExpression,
        GreaterThanExpression,
        LessThanExpression,
        GreaterThanOrEqualExpression,
        LessThanOrEqualExpression,
        IsExpression,
        UnaryPlusExpression,
        UnaryMinusExpression,
        LogicalNotExpression,
        BitwiseNotExpression,
        TrueLiteralExpression,
        FalseLiteralExpression,
        NullLiteralExpression,
        PreIncrementExpression,
        PostIncrementExpression,
        PreDecrementExpression,
        PostDecrementExpression,
        SizeOfExpression,
        TypeOfExpression,
        CoalesceExpression,
        ThisExpression
    };

    // Note, internal variables start with "$" and must be replaced before CSharp syntax analyzer.
    // This data will be restored after CSharp syntax analyzer in IdentifierName and StringLiteralExpression.
    std::string fixed_expression = expression;
    ReplaceInternalNames(fixed_expression);

    HRESULT Status = S_OK;
    void *pStackProgram = nullptr;
    IfFailRet(Interop::GenerateStackMachineProgram(fixed_expression, &pStackProgram, output));

    static constexpr int32_t ProgramInProgress = 0;
    static constexpr int32_t ProgramFinished = -1;
    int32_t Command = ProgramInProgress;
    void *pArguments = nullptr;

    m_evalData.pThread = pThread;
    m_evalData.frameLevel = frameLevel;

    do
    {
        if (FAILED(Status = Interop::NextStackCommand(pStackProgram, Command, &pArguments, output)) ||
            Command == ProgramFinished ||
            FAILED(Status = CommandImplementation.at(Command)(evalStack, pArguments, output, m_evalData)))
        {
            break;
        }
    } while (true);

    switch (Status)
    {
    case CORDBG_E_ILLEGAL_IN_PROLOG:
    case CORDBG_E_ILLEGAL_IN_NATIVE_CODE:
    case CORDBG_E_ILLEGAL_IN_STACK_OVERFLOW:
    case CORDBG_E_ILLEGAL_IN_OPTIMIZED_CODE:
    case CORDBG_E_ILLEGAL_AT_GC_UNSAFE_POINT:
        output = "This expression causes side effects and will not be evaluated. ";
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
        output = "Evaluation timed out, but function evaluation can't be completed or aborted. Debuggee have inconsistent state now.";
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

    Interop::ReleaseStackMachineProgram(pStackProgram);
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

    for (const auto &entry : corElementToValueNameMap)
    {
        typeDef = mdTypeDefNil;
        IfFailRet(trMDImport->FindTypeDefByName(entry.second, mdTypeDefNil, &typeDef));

        assert(m_evalData.trElementToValueClassMap.find(entry.first) == m_evalData.trElementToValueClassMap.end());
        m_evalData.trElementToValueClassMap.emplace(entry.first, nullptr);
        IfFailRet(pModule->GetClassFromToken(typeDef, &m_evalData.trElementToValueClassMap.at(entry.first)));
    }

    return S_OK;
}

} // namespace dncdbg
