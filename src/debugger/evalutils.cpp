// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evalutils.h"
#include "debuginfo/debuginfo.h"
#include "metadata/typeprinter.h"
#include "utils/utf.h"

namespace dncdbg::EvalUtils
{

namespace
{

std::vector<std::string> GatherParameters(const std::vector<std::string> &identifiers, int indexEnd)
{
    std::vector<std::string> result;
    for (int i = 0; i < indexEnd; i++)
    {
        std::string typeName;
        std::vector<std::string> params = ParseGenericParams(identifiers[i], typeName);
        result.insert(result.end(), params.begin(), params.end());
    }
    return result;
}

mdTypeDef GetTypeTokenForName(IMetaDataImport *pMDImport, mdTypeDef tkEnclosingClass, const std::string &name)
{
    mdTypeDef typeToken = mdTypeDefNil;
    pMDImport->FindTypeDefByName(to_utf16(name).c_str(), tkEnclosingClass, &typeToken);
    return typeToken;
}

HRESULT FindTypeInModule(ICorDebugModule *pModule, const std::vector<std::string> &identifiers,
                         int &nextIdentifier, mdTypeDef &typeToken)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    std::string currentTypeName;

    // Search for type in module
    for (int i = nextIdentifier; i < static_cast<int>(identifiers.size()); i++)
    {
        std::string name;
        ParseGenericParams(identifiers[i], name);
        currentTypeName += (currentTypeName.empty() ? "" : ".") + name;

        typeToken = GetTypeTokenForName(trMDImport, mdTypeDefNil, currentTypeName);
        if (typeToken != mdTypeDefNil)
        {
            nextIdentifier = i + 1;
            break;
        }
    }

    if (typeToken == mdTypeDefNil) // type not found, continue search in next module
    {
        return E_FAIL;
    }

    // Resolve nested class
    for (int j = nextIdentifier; j < static_cast<int>(identifiers.size()); j++)
    {
        std::string name;
        ParseGenericParams(identifiers[j], name);
        const mdTypeDef classToken = GetTypeTokenForName(trMDImport, typeToken, name);
        if (classToken == mdTypeDefNil)
        {
            break;
        }
        typeToken = classToken;
        nextIdentifier = j + 1;
    }

    return S_OK;
}

HRESULT ResolveParameters(const std::vector<std::string> &params, ICorDebugThread *pThread, DebugInfo *pDebugInfo,
                          std::vector<ToRelease<ICorDebugType>> &trTypes)
{
    HRESULT Status = S_OK;
    for (const auto &p : params)
    {
        ICorDebugType *tmpType = nullptr; // NOLINT(misc-const-correctness)
        IfFailRet(GetType(p, pThread, pDebugInfo, &tmpType));
        trTypes.emplace_back(tmpType);
    }
    return S_OK;
}

} // unnamed namespace

std::vector<std::string> ParseGenericParams(const std::string &identifier, std::string &typeName)
{
    std::vector<std::string> result;

    const std::size_t start = identifier.find('<');
    if (start == std::string::npos)
    {
        typeName = identifier;
        return result;
    }

    int paramDepth = 0;
    bool inArray = false;

    result.emplace_back("");

    for (std::size_t i = start; i < identifier.size(); i++)
    {
        const char c = identifier.at(i);
        switch (c)
        {
        case ',':
            if (paramDepth == 1 && !inArray)
            {
                result.emplace_back("");
                continue;
            }
            break;
        case '[':
            inArray = true;
            break;
        case ']':
            inArray = false;
            break;
        case '<':
            paramDepth++;
            if (paramDepth == 1)
            {
                continue;
            }
            break;
        case '>':
            paramDepth--;
            if (paramDepth == 0)
            {
                continue;
            }
            break;
        default:
            break;
        }
        result.back() += c;
    }
    typeName = identifier.substr(0, start) + '`' + std::to_string(result.size());
    return result;
}

HRESULT GetType(const std::string &typeName, ICorDebugThread *pThread, DebugInfo *pDebugInfo, ICorDebugType **ppType)
{
    HRESULT Status = S_OK;
    std::vector<int> ranks;
    std::vector<std::string> classIdentifiers = ParseType(typeName, ranks);
    if (classIdentifiers.size() == 1)
    {
        classIdentifiers[0] = TypePrinter::RenameToSystem(classIdentifiers[0]);
    }

    ToRelease<ICorDebugType> trType;
    int nextClassIdentifier = 0;
    IfFailRet(FindType(classIdentifiers, nextClassIdentifier, pThread, pDebugInfo, nullptr, &trType));

    if (!ranks.empty())
    {
        ToRelease<ICorDebugAppDomain> trAppDomain;
        ToRelease<ICorDebugAppDomain2> trAppDomain2;
        IfFailRet(pThread->GetAppDomain(&trAppDomain));
        IfFailRet(trAppDomain->QueryInterface(IID_ICorDebugAppDomain2, reinterpret_cast<void **>(&trAppDomain2)));

        for (auto irank = ranks.rbegin(); irank != ranks.rend(); ++irank)
        {
            const ToRelease<ICorDebugType> trElementType(std::move(trType));
            IfFailRet(trAppDomain2->GetArrayOrPointerType(*irank > 1 ? ELEMENT_TYPE_ARRAY : ELEMENT_TYPE_SZARRAY, *irank,
                                                          trElementType,
                                                          &trType)); // NOLINT(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
        }
    }

    *ppType = trType.Detach();
    return S_OK;
}

std::vector<std::string> ParseType(const std::string &expression, std::vector<int> &ranks)
{
    std::vector<std::string> result;
    int paramDepth = 0;

    result.emplace_back();

    for (const char c : expression)
    {
        switch (c)
        {
        case '.':
            if (paramDepth == 0)
            {
                result.emplace_back();
                continue;
            }
            break;
        case '[':
            if (paramDepth == 0)
            {
                ranks.push_back(1);
                continue;
            }
            break;
        case ']':
            if (paramDepth == 0)
            {
                continue;
            }
            break;
        case ',':
            if (paramDepth == 0)
            {
                if (!ranks.empty())
                {
                    ranks.back()++;
                }
                continue;
            }
            break;
        case '<':
            paramDepth++;
            break;
        case '>':
            paramDepth--;
            break;
        case ' ':
            continue;
        default:
            break;
        }
        result.back() += c;
    }
    return result;
}

HRESULT FindType(const std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 DebugInfo *pDebugInfo, ICorDebugModule *pModule, ICorDebugType **ppType, ICorDebugModule **ppModule)
{
    HRESULT Status = S_OK;

    if (pModule != nullptr)
    {
        pModule->AddRef();
    }
    ToRelease<ICorDebugModule> trTypeModule(pModule);

    mdTypeDef typeToken = mdTypeDefNil;

    if (trTypeModule == nullptr)
    {
        IfFailRet(pDebugInfo->ForEachModule(
            [&](ICorDebugModule *pModule) -> HRESULT
            {
                if (typeToken != mdTypeDefNil) // already found
                {
                    return S_FALSE; // Fast exit from cycle
                }

                if (SUCCEEDED(FindTypeInModule(pModule, identifiers, nextIdentifier, typeToken)))
                {
                    pModule->AddRef();
                    trTypeModule = pModule;
                }
                return S_OK; // Return with success to continue walk.
            }));
    }
    else
    {
        FindTypeInModule(trTypeModule, identifiers, nextIdentifier, typeToken);
    }

    if (typeToken == mdTypeDefNil)
    {
        return E_FAIL;
    }

    if (ppType != nullptr)
    {
        const std::vector<std::string> params = GatherParameters(identifiers, nextIdentifier);
        std::vector<ToRelease<ICorDebugType>> trTypes;
        IfFailRet(ResolveParameters(params, pThread, pDebugInfo, trTypes));

        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trTypeModule->GetClassFromToken(typeToken, &trClass));

        ToRelease<ICorDebugClass2> trClass2;
        IfFailRet(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2)));

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trTypeModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        DWORD flags = 0;
        ULONG nameLen = 0;
        mdToken tkExtends = mdTokenNil;
        IfFailRet(trMDImport->GetTypeDefProps(typeToken, nullptr, 0, &nameLen, &flags, &tkExtends));

        std::string eTypeName;
        IfFailRet(TypePrinter::NameForToken(tkExtends, trMDImport, eTypeName, true, nullptr));

        const bool isValueType = eTypeName == "System.ValueType" || eTypeName == "System.Enum";
        const CorElementType et = isValueType ? ELEMENT_TYPE_VALUETYPE : ELEMENT_TYPE_CLASS;

        ToRelease<ICorDebugType> trType;
        IfFailRet(trClass2->GetParameterizedType(et, static_cast<uint32_t>(trTypes.size()),
                                                 reinterpret_cast<ICorDebugType **>(trTypes.data()), &trType));

        *ppType = trType.Detach();
    }
    if (ppModule != nullptr)
    {
        *ppModule = trTypeModule.Detach();
    }

    return S_OK;
}

} // namespace dncdbg::EvalUtils
