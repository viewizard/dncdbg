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
#include <string>

namespace dncdbg::EvalDebugInfoHelpers
{

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

} // namespace dncdbg::EvalDebugInfoHelpers
