// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalhelpers.h"
#include "debugger/evalstackmachine.h"
#include "debugger/evaluator.h"
#include "debugger/valueprint.h"
#include "metadata/modules.h"
#include "utils/hresult.h"
#include <string_view>
#include <unordered_map>

namespace dncdbg
{

namespace
{

mdMethodDef GetMethodToken(IMetaDataImport *pMDImport, mdTypeDef typeDef, const WSTRING &methodName)
{
    ULONG numMethods = 0;
    HCORENUM mEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    pMDImport->EnumMethodsWithName(&mEnum, typeDef, methodName.c_str(), &methodDef, 1, &numMethods);
    pMDImport->CloseEnum(mEnum);
    return methodDef;
}

} // unnamed namespace

// From strike.cpp
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull)
{
    *ppOutputValue = nullptr;
    if (pIsNull != nullptr)
    {
        *pIsNull = FALSE;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trCurrentValue(pValue);
    HRESULT Status = S_OK;

    while (true)
    {
        ToRelease<ICorDebugReferenceValue> trReferenceValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue))))
        {
            BOOL isNull = FALSE;
            IfFailRet(trReferenceValue->IsNull(&isNull));
            if (isNull == FALSE)
            {
                ToRelease<ICorDebugValue> trDereferencedValue;
                IfFailRet(trReferenceValue->Dereference(&trDereferencedValue));
                trCurrentValue = trDereferencedValue.Detach();
                continue;
            }
            else
            {
                if (pIsNull != nullptr)
                {
                    *pIsNull = TRUE;
                }
                break; // unboxed until null reference
            }
        }

        ToRelease<ICorDebugBoxValue> trBoxedValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugBoxValue, reinterpret_cast<void **>(&trBoxedValue))))
        {
            ToRelease<ICorDebugObjectValue> trUnboxedValue;
            IfFailRet(trBoxedValue->GetObject(&trUnboxedValue));
            trCurrentValue = trUnboxedValue.Detach();
            continue;
        }

        break; // unboxed until object
    }

    trCurrentValue->AddRef();
    *ppOutputValue = trCurrentValue;
    return S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    if (trType == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ToRelease<ICorDebugObjectValue> trObjValue;
    ToRelease<ICorDebugValue> trUnboxedResultValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &trUnboxedResultValue));
    IfFailRet(trUnboxedResultValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(trMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        WSTRING mdName(nameLen, '\0');
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L24
        if (mdName == W("value"))
        {
            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppValueValue));
        }

        // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L23
        if (mdName == W("hasValue"))
        {
            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppHasValueValue));
        }
    }

    return (*ppValueValue == nullptr || *ppHasValueValue == nullptr) ? E_FAIL : S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, bool &hasValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trHasValueValue;
    IfFailRet(GetNullableValue(pValue, ppValueValue, &trHasValueValue));

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trHasValueValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    uint8_t boolHasValue = 0;
    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    if (cbSize != sizeof(boolHasValue))
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&boolHasValue));

    hasValue = (boolHasValue == 1);

    return S_OK;
}

void ParseFormatSpecifier(const std::string &expressionWithFormat, std::string &expression, FormatSpecifier &specifier)
{
    // Format specifiers
    // https://learn.microsoft.com/en-us/visualstudio/debugger/format-specifiers-in-csharp?view=visualstudio
    static const std::unordered_map<std::string_view, FormatSpecifier> formatMap{
        {"ac",      FormatSpecifier::ForceEvaluation},
        {"d",       FormatSpecifier::DecimalInteger},
        {"h",       FormatSpecifier::HexadecimalInteger},
        {"dynamic", FormatSpecifier::Dynamic},
        {"nse",     FormatSpecifier::EvaluatesWithNoSideEffects},
        {"nq",      FormatSpecifier::StringWithNoQuotes},
        {"hidden",  FormatSpecifier::DisplaysHiddenMembers},
        {"raw",     FormatSpecifier::DisplaysInRawMode},
        {"results", FormatSpecifier::Results}
    };

    specifier = FormatSpecifier::None;
    expression = expressionWithFormat;

    // Find the last comma to isolate the potential suffix
    size_t commaPos = expression.rfind(',');

    while (commaPos != std::string::npos)
    {
        // Extract the tail substring strictly after the comma
        const std::string_view tail = std::string_view(expression).substr(commaPos + 1);

        auto find = formatMap.find(tail);
        if (find == formatMap.end())
        {
            // Stop as soon as a comma-separated tail is not a known specifier:
            // the remaining text is the actual expression, which may legitimately
            // contain commas (e.g. multi-dimensional array access like arr[0,1]).
            break;
        }

        specifier = specifier | find->second;
        expression.resize(commaPos);

        commaPos = expression.rfind(',');
    }
}

HRESULT FindFunctionInModule(ICorDebugThread *pThread, const std::string &moduleFileName, const WSTRING &typeName,
                             const WSTRING &methodName, ICorDebugFunction **ppFunction)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(Modules::GetModuleWithName(pThread, moduleFileName, &trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trMDImport->FindTypeDefByName(typeName.c_str(), mdTypeDefNil, &typeDef));

    const mdMethodDef methodDef = GetMethodToken(trMDImport, typeDef, methodName);

    if (methodDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    return trModule->GetFunctionFromToken(methodDef, ppFunction);
}

void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts)
{
    size_t pos = 0;
    size_t prevPos = 0;

    while ((pos = textWithEval.find('{', prevPos)) != std::string::npos)
    {
        // Add text before the '{' (if any) as literal text.
        if (pos > prevPos)
        {
            textWithEvalParts.emplace_back(textWithEval.substr(prevPos, pos - prevPos), false);
        }

        // Find the matching closing '}' by counting brace depth.
        size_t endPos = pos + 1;
        int braceDepth = 1;
        while (endPos < textWithEval.length() && braceDepth > 0)
        {
            if (textWithEval.at(endPos) == '{')
            {
                braceDepth++;
            }
            else if (textWithEval.at(endPos) == '}')
            {
                braceDepth--;
            }
            endPos++;
        }

        if (braceDepth > 0)
        {
            // No matching closing brace found, treat from '{' to end as literal text.
            textWithEvalParts.emplace_back(textWithEval.substr(pos), false);
            prevPos = textWithEval.length();
            break;
        }

        // Add the expression inside braces (without the braces themselves) as expression.
        // endPos points to position after the matching '}', so expression is [pos+1, endPos-1).
        textWithEvalParts.emplace_back(textWithEval.substr(pos + 1, endPos - pos - 2), true);

        prevPos = endPos;
    }

    // Add remaining text after the last '}' (or entire string if no braces found) as literal text.
    if (prevPos < textWithEval.length())
    {
        textWithEvalParts.emplace_back(textWithEval.substr(prevPos), false);
    }
}

void BuildTextWithEval(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread, ICorDebugValue *pForcedThisValue,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output)
{
    // Build the final output text by evaluating expressions.
    for (const auto &[text, isExpression] : textWithEvalParts)
    {
        if (!isExpression)
        {
            // Literal text - append directly.
            output += text;
        }
        else
        {
            // Expression - evaluate it.
            FormatSpecifier specifier = FormatSpecifier::None;
            std::string expression;
            ParseFormatSpecifier(text, expression, specifier);

            std::string value;
            std::string errorText;
            ToRelease<ICorDebugValue> trResultValue;
            if (SUCCEEDED(pEvalStackMachine->EvaluateExpression(pThread, FrameLevel{0}, expression,
                                                                pForcedThisValue == nullptr ? specifier : specifier | FormatSpecifier::DisplaysInRawMode,
                                                                pForcedThisValue, &trResultValue, errorText)) &&
                SUCCEEDED(PrintValue(pThread, pEvaluator, pEvalStackMachine, trResultValue, specifier, value)))
            {
                output += value;
            }
            else
            {
                if (!errorText.empty())
                {
                    output += "{" + errorText + "}";
                }
                else
                {
                    output += "{unknown error}";
                }
            }
        }
    }
}

} // namespace dncdbg
