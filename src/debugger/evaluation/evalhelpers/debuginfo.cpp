// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/debuginfo.h"
#include "debugger/frames.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <list>

namespace dncdbg::EvalDebugInfoHelpers
{

namespace
{

HRESULT ForEachMethod(ICorDebugModule *pModule, const std::function<bool(const std::string &, mdMethodDef &)> &functor)
{
    HRESULT Status = S_OK;
    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG fetched = 0;
    HCORENUM fTypeEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;

    while (SUCCEEDED(trMDImport->EnumTypeDefs(&fTypeEnum, &typeDef, 1, &fetched)) && fetched != 0)
    {
        std::string displayTypeName;
        IfFailRet(MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, nullptr));

        HCORENUM fFuncEnum = nullptr;
        mdMethodDef mdMethod = mdMethodDefNil;
        fetched = 0;

        while (SUCCEEDED(trMDImport->EnumMethods(&fFuncEnum, typeDef, &mdMethod, 1, &fetched)) && fetched != 0)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> szFuncName(nameLen, '\0');
            if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, szFuncName.data(), nameLen, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Get the generic type parameters of the method.
            ToRelease<IMetaDataImport2> trMDImport2;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

            HCORENUM fGenEnum = nullptr;
            mdGenericParam genParam = mdGenericParamNil;
            fetched = 0;
            std::string genParams;

            while (SUCCEEDED(trMDImport2->EnumGenericParams(&fGenEnum, mdMethod, &genParam, 1, &fetched)) && fetched == 1)
            {
                ULONG genNameLen = 0;
                if (FAILED(trMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &genNameLen)))
                {
                    continue;
                }

                std::vector<WCHAR> szGenName(genNameLen, '\0');
                if (FAILED(trMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr,
                                                             szGenName.data(), genNameLen, nullptr)))
                {
                    continue;
                }

                // Append a comma after each element; the trailing comma is stripped later.
                genParams += to_utf8(szGenName.data()) + ",";
            }

            trMDImport2->CloseEnum(fGenEnum);

            std::string fullName = to_utf8(szFuncName.data());
            if (!genParams.empty())
            {
                // Remove the trailing comma, it is no longer needed.
                genParams.pop_back();
                fullName += "<" + genParams + ">";
            }

            fullName.insert(0, displayTypeName + '.');
            if (!functor(fullName, mdMethod))
            {
                trMDImport->CloseEnum(fFuncEnum);
                trMDImport->CloseEnum(fTypeEnum);
                return E_FAIL;
            }
        }

        trMDImport->CloseEnum(fFuncEnum);
    }
    trMDImport->CloseEnum(fTypeEnum);

    return S_OK;
}

std::vector<std::string> SplitOnTokens(const std::string &str, const char delim)
{
    std::vector<std::string> res;
    size_t prev = 0;

    while (true)
    {
        const size_t pos = str.find(delim, prev);
        if (pos == std::string::npos)
        {
            res.emplace_back(str, prev);
            break;
        }

        res.emplace_back(str, prev, pos - prev);
        prev = pos + 1;
    }

    return res;
}

bool IsTargetFunction(const std::vector<std::string> &fullName, const std::vector<std::string> &targetName)
{
    // The function is matched by name components: the requested target function name must fully or partially match
    // the trailing components of the real function name. For example:
    //
    // "MethodA" matches
    // Program.ClassA.MethodA
    // Program.ClassB.MethodA
    // Program.ClassA.InnerClass.MethodA
    //
    // "ClassA.MethodB" matches
    // Program.ClassA.MethodB
    // Program.ClassB.ClassA.MethodB

    auto fullIt = fullName.rbegin();
    for (auto it = targetName.rbegin(); it != targetName.rend(); it++)
    {
        if (fullIt == fullName.rend() || *it != *fullIt)
        {
            return false;
        }

        fullIt++;
    }

    return true;
}

HRESULT ResolveMethodInModule(ICorDebugModule *pModule, const std::string &funcName, const ResolveFunctionBreakpointCallback &cb)
{
    std::vector<std::string> splitName = SplitOnTokens(funcName, '.');

    const auto functor = [&](const std::string &fullName, mdMethodDef &mdMethod) -> bool
        {
            const std::vector<std::string> splitFullName = SplitOnTokens(fullName, '.');

            // The target function has been found.
            if (IsTargetFunction(splitFullName, splitName))
            {
                if (FAILED(cb(pModule, mdMethod)))
                {
                    return false; // Abort the operation.
                }
            }

            return true; // Continue with other functions that have a matching name.
        };

    return ForEachMethod(pModule, functor);
}

}

void GetImportsAndAliases(ICorDebugThread *pThread, FrameLevel frameLevel, PDB::ImportsAndAliases &pdbImports)
{
    const auto getImportsAndAliases = [&]() -> HRESULT
    {
        HRESULT Status = S_OK;
        ToRelease<ICorDebugFrame> trFrame;
        IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(trFrame->GetFunction(&trFunction));

        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trFunction->GetModule(&trModule));

        mdMethodDef methodDef = mdMethodDefNil;
        IfFailRet(trFunction->GetToken(&methodDef));

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        uint32_t currentIlOffset = 0;
        CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
        IfFailRet(trILFrame->GetIP(&currentIlOffset, &mappingResult));
        if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
            mappingResult == MAPPING_NO_INFO)
        {
            return E_FAIL;
        }

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        IfFailRet(DebugInfo::GetImportsAndAliases(trModule, methodDef, currentIlOffset, pdbImports));

        const auto applyTokenName = [&trMDImport](std::vector<PDB::Imports> &alias) -> HRESULT
        {
            for (auto &entry : alias)
            {
                // For TypeSpec tokens, pre-resolve the generic type arguments from the signature
                // so that GetFQDisplayNameForToken() can substitute them into the display name.
                std::list<std::string> args;
                std::list<std::string> *pArgs = nullptr;
                if (TypeFromToken(entry.token) == mdtTypeSpec)
                {
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    SigElementType sigType;
                    if (FAILED(trMDImport->GetTypeSpecFromToken(entry.token, &pSig, &cbSig)) ||
                        FAILED(ParseElementType(trMDImport, pSig, pSig + cbSig, 0, sigType, &args, true)))
                    {
                        // Skip entries whose TypeSpec signature cannot be parsed.
                        continue;
                    }
                    pArgs = &args;
                }

                if (FAILED(MetadataHelpers::GetFQDisplayNameForToken(entry.token, trMDImport, entry.displayName, pArgs)))
                {
                    // Skip entries whose target type cannot be resolved.
                    continue;
                }
            }

            return S_OK;
        };

        const auto importType = pdbImports.find(PDB::ImportsKind::ImportType);
        if (importType != pdbImports.cend())
        {
            applyTokenName(importType->second);
        }

        const auto aliasType = pdbImports.find(PDB::ImportsKind::AliasType);
        if (aliasType != pdbImports.cend())
        {
            applyTokenName(aliasType->second);
        }

        return S_OK;
    };

    pdbImports.clear();
    getImportsAndAliases();

    // In case of failure (or no debug info for this code), add the default "System" namespace.
    auto &importNamespace = pdbImports[PDB::ImportsKind::ImportNamespace];
    if (importNamespace.empty())
    {
        importNamespace.emplace_back();
        importNamespace.back().targetNamespace = "System";
    }
}

HRESULT ResolveFunctionBreakpointInAny(const std::string &funcname, const ResolveFunctionBreakpointCallback &cb)
{
    return DebugInfo::GetEachPDBInfo([&](const PDBInfo &pdbInfo) -> HRESULT
        {
            ResolveMethodInModule(pdbInfo.m_trModule, funcname, cb);
            return S_OK;
        });
}

HRESULT ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, const std::string &funcname,
                                          const ResolveFunctionBreakpointCallback &cb)
{
    return ResolveMethodInModule(pModule, funcname, cb);
}

} // namespace dncdbg::EvalDebugInfoHelpers
