// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/stackmachine/handler_invocation.h"
#include "debugger/evaluation/evalhelpers/debuginfo.h"
#include "debugger/evaluation/evalhelpers/evalresolver.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/value.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/print.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace dncdbg::EvalStackMachine
{

HRESULT InvocationExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    const uint32_t argCount = opcode.count;

    HRESULT Status = S_OK;
    std::vector<ToRelease<ICorDebugValue>> trArgs(argCount);
    uint32_t tmpArgCount = argCount;
    while (tmpArgCount > 0)
    {
        tmpArgCount--;
        IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trArgs.at(tmpArgCount), nullptr, output));
        evalStack.pop_front();
    }

    if (evalStack.front().preventBinding)
    {
        return S_OK;
    }

    assert(!evalStack.front().identifiers.empty()); // We must have at least a method name (identifier).

    // TODO: locally defined functions (the compiler will create such functions with names like `<Calc1>g__Calc2|0_0`)
    const std::string methodDisplayName = evalStack.front().identifiers.back();
    evalStack.front().identifiers.pop_back();

    std::string funcName;
    const std::vector<std::string> genericMethodFQDisplayTypeNames = MetadataHelpers::ConvertDisplayToMetadataName(methodDisplayName, funcName);
    const size_t pos = funcName.find('`');
    if (pos != std::string::npos)
    {
        funcName.resize(pos);
    }

    PDB::ImportsAndAliases pdbImports;
    EvalDebugInfoHelpers::GetImportsAndAliases(ed.pThread, ed.frameLevel, pdbImports);

    bool idsEmpty = false;
    bool isInstance = true;
    ICorDebugValue *pForcedThisValue = evalStack.front().trValue == nullptr ? ed.pForcedThisValue : evalStack.front().trValue;

    std::vector<std::vector<std::string>> allIdentifiers;
    allIdentifiers.emplace_back(evalStack.front().identifiers);

    if (pForcedThisValue == nullptr && evalStack.front().identifiers.empty())
    {
        idsEmpty = true;
        std::string displayTypeName;
        IfFailRet(EvalMetadataHelpers::GetFQDisplayTypeName(ed.pThread, ed.frameLevel, displayTypeName, isInstance));
        if (isInstance)
        {
            allIdentifiers.back().emplace_back("this");
        }
        else
        {
            allIdentifiers.back() = MetadataHelpers::SplitFQDisplayTypeName(displayTypeName);

            const auto importType = pdbImports.find(PDB::ImportsKind::ImportType);
            if (importType != pdbImports.cend())
            {
                for (const auto &entry : importType->second)
                {
                    // Skip entries whose target type display name could not be resolved.
                    if (entry.displayName.empty())
                    {
                        continue;
                    }

                    std::vector<std::string> typeIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(entry.displayName);

                    if (!typeIdentifiers.empty())
                    {
                        allIdentifiers.emplace_back(std::move(typeIdentifiers));
                    }
                }
            }
        }
    }

    std::vector<SigElementType> funcArgs(argCount);
    for (uint32_t i = 0; i < argCount; ++i)
    {
        ToRelease<ICorDebugValue> trValueArg;
        IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(trArgs.at(i), &trValueArg, nullptr));
        IfFailRet(GetArgData(trValueArg, funcArgs.at(i).metadataTypeName, funcArgs.at(i).elemType));
    }

    std::vector<SigElementType> genericMethodParameters;
    genericMethodParameters.reserve(genericMethodFQDisplayTypeNames.size());
    std::transform(genericMethodFQDisplayTypeNames.cbegin(), genericMethodFQDisplayTypeNames.cend(), std::back_inserter(genericMethodParameters),
                   [&ed, &pdbImports](const auto &displayTypeName)
                   {
                       return EvalMetadataHelpers::GetSigElementTypeByDisplayTypeName(ed.pThread, displayTypeName, pdbImports);
                   });

    const auto expectedMethodGenParamCount = static_cast<uint32_t>(evalStack.front().trGenericTypeCache.size());
    ToRelease<ICorDebugFunction> trFunc;
    ToRelease<ICorDebugValue> trValue;
    ToRelease<ICorDebugType> trType;
    const auto resolveFunc = [&](std::vector<std::string> &testIdentifiers) -> HRESULT
    {
        trValue.Free();
        trType.Free();
        if (FAILED(Status = EvalResolver::ResolveIdentifiers(ed.pThread, ed.frameLevel, pForcedThisValue, nullptr,
                                                             testIdentifiers, ed.specifier, &trValue, nullptr, nullptr, &trType)))
        {
            if (pForcedThisValue == nullptr && !testIdentifiers.empty())
            {
                const auto importType = pdbImports.find(PDB::ImportsKind::ImportType);
                if (importType != pdbImports.cend())
                {
                    for (const auto &entry : importType->second)
                    {
                        std::vector<std::string> testIdents = testIdentifiers;
                        const std::vector<std::string> importTypeIdents = MetadataHelpers::SplitFQDisplayTypeName(entry.displayName);
                        testIdents.insert(testIdents.begin(), importTypeIdents.cbegin(), importTypeIdents.cend());

                        if (SUCCEEDED(EvalResolver::ResolveIdentifiers(ed.pThread, ed.frameLevel, pForcedThisValue, nullptr,
                                                                       testIdents, ed.specifier, &trValue, nullptr, nullptr, &trType)))
                        {
                            break;
                        }
                    }
                }
            }

            if (trType == nullptr && trValue == nullptr)
            {
                return Status;
            }
        }

        CorElementType elemType = ELEMENT_TYPE_MAX;

        bool searchStatic = false;
        if (trType != nullptr)
        {
            searchStatic = true;
            IfFailRet(trType->GetType(&elemType));
        }
        else
        {
            IfFailRet(trValue->GetType(&elemType));

            if (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
            {
                // Create a proper System.Array type in order to walk methods.
                ToRelease<ICorDebugClass> trClass;
                IfFailRet(SystemTypes::GetClass(SystemTypes::SystemType::Array, &trClass));
                ToRelease<ICorDebugClass2> trClass2;
                IfFailRet(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2)));
                IfFailRet(trClass2->GetParameterizedType(ELEMENT_TYPE_CLASS, 0, nullptr, &trType));
            }
            else
            {
                // Boxing a built-in element type into a value type in order to call methods.
                // Note, ELEMENT_TYPE_STRING is a reference type and does not need boxing.
                ToRelease<ICorDebugClass> trClass;
                if (elemType != ELEMENT_TYPE_STRING &&
                    SUCCEEDED(SystemTypes::GetClass(elemType, &trClass)))
                {
                    uint32_t cbSize = 0;
                    IfFailRet(trValue->GetSize(&cbSize));
                    std::vector<uint8_t> elemValue(cbSize, 0);

                    ToRelease<ICorDebugGenericValue> trGenericValue;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
                    IfFailRet(trGenericValue->GetValue(static_cast<void *>(elemValue.data())));

                    trValue.Free();
                    IfFailRet(EvalExec::CreateValueType(ed.pThread, trClass, elemValue.data(), &trValue));
                }

                ToRelease<ICorDebugValue2> trValue2;
                IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                IfFailRet(trValue2->GetExactType(&trType));
            }

            if (trType == nullptr)
            {
                return E_NOTIMPL;
            }
        }

        ToRelease<ICorDebugType> trResultType;
        IfFailRet(Walkers::WalkMethods(trType, true, &trResultType,
                [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
                    std::vector<SigElementType> &methodArgs, uint32_t methodGenParamCount,
                    const Walkers::GetFunctionCallback &getFunction) -> HRESULT
            {
                if ((searchStatic && !isStatic) || (!searchStatic && isStatic && !idsEmpty) ||
                    funcName != methodName || funcArgs.size() != methodArgs.size() ||
                    methodGenParamCount != expectedMethodGenParamCount)
                {
                    return S_OK; // Return success to continue walking.
                }

                for (size_t i = 0; i < funcArgs.size(); ++i)
                {
                    if (FAILED(ApplyGenericMethodParameters(genericMethodParameters, methodArgs.at(i))) ||
                        // TODO: implicit casts must be handled here.
                        funcArgs.at(i) != methodArgs.at(i))
                    {
                        return S_OK; // Return success to continue walking.
                    }
                }

                IfFailRet(getFunction(&trFunc));
                isInstance = !isStatic;

                return S_CAN_EXIT; // Fast exit from the loop.
            }));

        if (trFunc == nullptr && !searchStatic)
        {
            // Restore the initial array type, since we need the proper type name in WalkExtensionMethods().
            // Note: in case of a generic extension method call, we also need the proper type parameters.
            if (trValue != nullptr && (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY))
            {
                trType.Free();
                ToRelease<ICorDebugValue2> trValue2;
                IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                IfFailRet(trValue2->GetExactType(&trType));
            }

            bool hasThisTypeParams = false;
            IfFailRet(Walkers::WalkExtensionMethods(trType, elemType,
                [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
                    std::vector<SigElementType> &methodArgs, uint32_t methodGenParamCount,
                    const Walkers::GetFunctionCallback &getFunction) -> HRESULT
                {
                    // Extension methods must provide `this` as the first argument.
                    assert(!isStatic);

                    // Note: extension methods explicitly provide `this` as the first argument in methodArgs.
                    if ((funcArgs.size() + 1) != methodArgs.size() || funcName != methodName)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // Determine whether the `this` parameter type is itself generic (e.g. `IEnumerable<T>`)
                    // or its enclosing types are. In that case, the source type's generic parameters are used
                    // to fill the method's generic parameter slots; otherwise trType is dropped to avoid
                    // injecting the wrong parameters.
                    hasThisTypeParams = false;
                    if (!methodArgs.at(0).metadataTypeName.empty())
                    {
                        hasThisTypeParams = (MetadataHelpers::ParseTotalGenericArity(methodArgs.at(0).metadataTypeName) != 0);
                    }

                    if (!hasThisTypeParams &&
                        methodGenParamCount != expectedMethodGenParamCount)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    for (size_t i = 0; i < funcArgs.size(); ++i)
                    {
                        if (FAILED(ApplyGenericMethodParameters(genericMethodParameters, methodArgs.at(i + 1))) ||
                            // TODO: implicit casts must be handled here.
                            funcArgs.at(i) != methodArgs.at(i + 1))
                        {
                            return S_OK; // Return success to continue walking.
                        }
                    }

                    IfFailRet(getFunction(&trFunc));
                    // Extension methods are invoked as instance methods.
                    isInstance = true;

                    return S_CAN_EXIT; // Fast exit from the loop.
                }));

            if (!hasThisTypeParams)
            {
                trType.Free();
            }
        }
        else if (trFunc != nullptr && trResultType != nullptr)
        {
            trType = trResultType.Detach();
        }

        return trFunc != nullptr ? S_OK : E_INVALIDARG;
    };

    for (auto &idents : allIdentifiers)
    {
        if (SUCCEEDED(resolveFunc(idents)))
        {
            break;
        }
    }

    if (trFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    const uint32_t realArgsCount = argCount + (isInstance ? 1 : 0);
    std::vector<ICorDebugValue *> pValueArgs;
    pValueArgs.reserve(realArgsCount);

    // Place the instance value ("this") for extension or non-static methods.
    if (isInstance)
    {
        pValueArgs.emplace_back(trValue.GetPtr());
    }

    // Add argument values.
    for (uint32_t i = 0; i < argCount; i++)
    {
        pValueArgs.emplace_back(trArgs.at(i).GetPtr());
    }

    // Prepare the method's generic types, if any.
    std::vector<ToRelease<ICorDebugType>> trMethodGenericTypes;
    trMethodGenericTypes.reserve(evalStack.front().trGenericTypeCache.size());
    for (size_t i = evalStack.front().trGenericTypeCache.size(); i > 0; i--)
    {
        // Note: we detach here, since trGenericTypeCache will be cleared at the ResetEntry() call anyway.
        trMethodGenericTypes.emplace_back(evalStack.front().trGenericTypeCache.at(i - 1).Detach());
    }

    evalStack.front().ResetEntry();
    Status = EvalExec::CallFunction(ed.pThread, trFunc, trType, trMethodGenericTypes.empty() ? nullptr : &trMethodGenericTypes,
                                    pValueArgs.data(), static_cast<uint32_t>(pValueArgs.size()), ed.specifier, &evalStack.front().trValue);

    // CORDBG_S_FUNC_EVAL_HAS_NO_RESULT: Some Func evals will lack a return value, such as those whose return type is void.
    if (Status == CORDBG_S_FUNC_EVAL_HAS_NO_RESULT)
    {
        // We can't create ELEMENT_TYPE_VOID, so we are forced to use System.Void instead.
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(SystemTypes::GetClass(SystemTypes::SystemType::Void, &trClass));
        IfFailRet(EvalExec::CreateValueType(ed.pThread, trClass, nullptr, &evalStack.front().trValue));
    }

    return Status;
}

// ObjectCreationExpression (e.g. `new Guid("...")`, `new Point(1, 2)`) --
// plain constructor calls only, no object/collection initializers (see the
// object_creation_expression parser handler in parser.cpp, which rejects
// those before this ever runs).
HRESULT ObjectCreationExpression(const Parser::Opcode &opcode, std::list<EvalStackEntry> &evalStack, std::string &output, const EvalData &ed)
{
    const uint32_t argCount = opcode.count;
    HRESULT Status = S_OK;

    std::vector<ToRelease<ICorDebugValue>> trArgs(argCount);
    uint32_t tmpArgCount = argCount;
    while (tmpArgCount > 0)
    {
        tmpArgCount--;
        IfFailRet(GetFrontStackEntryValue(evalStack, ed, &trArgs.at(tmpArgCount), nullptr, output));
        evalStack.pop_front();
    }

    // The type identifier(s) (e.g. "Guid", "System.Guid", "List<int>") are
    // what's left in the front entry once all constructor arguments have
    // been popped -- the same shape InvocationExpression relies on for its
    // "function" entry above, resolved as a *type* rather than a value via
    // GetFrontStackEntryType (the same helper SizeOfExpression uses).
    ToRelease<ICorDebugType> trType;
    IfFailRet(GetFrontStackEntryType(evalStack, ed, &trType, output));

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(trType->GetType(&elemType));
    if (elemType != ELEMENT_TYPE_CLASS && elemType != ELEMENT_TYPE_VALUETYPE)
    {
        output = "error: 'new' is only supported for class and struct types";
        return E_NOTIMPL;
    }

    std::vector<SigElementType> funcArgs(argCount);
    for (uint32_t i = 0; i < argCount; ++i)
    {
        ToRelease<ICorDebugValue> trValueArg;
        IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(trArgs.at(i), &trValueArg, nullptr));
        IfFailRet(GetArgData(trValueArg, funcArgs.at(i).metadataTypeName, funcArgs.at(i).elemType));
    }

    // Constructors aren't inherited in C# -- unlike the InvocationExpression
    // method lookup above, this never walks to a base type.
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Walkers::WalkMethods(trType, false, nullptr,
        [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
           std::vector<SigElementType> &methodArgs, uint32_t /*methodGenParamCount*/,
           const Walkers::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (isStatic || methodName != ".ctor" || funcArgs.size() != methodArgs.size())
            {
                return S_OK; // Return success to continue walking.
            }

            for (size_t i = 0; i < funcArgs.size(); ++i)
            {
                // TODO: implicit casts must be handled here.
                if (funcArgs.at(i) != methodArgs.at(i))
                {
                    return S_OK; // Return success to continue walking.
                }
            }

            IfFailRet(getFunction(&trFunc));
            return S_CAN_EXIT; // Fast exit from the loop.
        }));

    if (trFunc == nullptr)
    {
        std::string typeName;
        MetadataHelpers::GetFQDisplayTypeName(trType, typeName);
        output = "error: '" + typeName + "' has no accessible constructor taking " +
                 IntegerToString(argCount) + " argument" + (argCount == 1 ? "" : "s");
        return E_INVALIDARG;
    }

    // Generic type arguments for the type itself (e.g. the `int` in
    // `List<int>`) -- constructor func-evals need these even though the
    // constructor method itself isn't generic.
    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    if (SUCCEEDED(trType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ICorDebugType *pCurType = nullptr;
        ULONG fetched = 0;
        while (SUCCEEDED(trTypeEnum->Next(1, &pCurType, &fetched)) && fetched == 1)
        {
            trTypeParams.emplace_back(pCurType);
        }
    }

    std::vector<ICorDebugValue *> pValueArgs;
    pValueArgs.reserve(argCount);
    for (uint32_t i = 0; i < argCount; i++)
    {
        pValueArgs.emplace_back(trArgs.at(i).GetPtr());
    }

    evalStack.front().ResetEntry();
    return EvalExec::CallConstructor(ed.pThread, trFunc, trTypeParams,
                                     pValueArgs.empty() ? nullptr : pValueArgs.data(),
                                     static_cast<uint32_t>(pValueArgs.size()), &evalStack.front().trValue);
}

} // namespace dncdbg::EvalStackMachine
