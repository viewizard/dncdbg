// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_METADATA_H
#define DEBUGGER_EVALUATION_EVALHELPERS_METADATA_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include "metadata/sigparse.h"
#include "types/types.h"
#include "utils/utf.h"
#include <functional>
#include <string>

namespace dncdbg::EvalMetadataHelpers
{

using WalkFieldsCallback = std::function<HRESULT(mdFieldDef)>;
using WalkPropertiesCallback = std::function<HRESULT(mdProperty)>;

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachFields(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkFieldsCallback &cb);

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachProperties(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkPropertiesCallback &cb);

HRESULT FindThisProxyFieldValue(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                ICorDebugValue *pInputValue, ICorDebugValue **ppResultValue);

HRESULT GetFirstUserCodeEnclosingClass(IMetaDataImport *pMDImport, mdTypeDef typeDef, mdTypeDef &userTypeDef);

// Get the fully-qualified "display" type name of the method's declaring type.
// Sets "haveThis" to true when the method has a "this" instance available.
HRESULT GetFQDisplayTypeName(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &displayTypeName, bool &haveThis);

// Get the fully-qualified "display" method name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeMethodName(ICorDebugFrame *pFrame, std::string &displayName);

// Note: this is a heavy function, since it is forced to search for the type in all modules to detect the proper CorElementType
// and the proper "metadata" type name. It provides a result equivalent to a ParseElementType() call with the `addElementTypeName = false` parameter.
SigElementType GetSigElementTypeByDisplayTypeName(ICorDebugThread *pThread, const std::string &displayTypeName,
                                                  const PDB::ImportsAndAliases &pdbImports);

// Note: `identifiers` contain "display" names and are converted into "metadata" names for lookup inside method logic.
HRESULT FindType(std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, const PDB::ImportsAndAliases &pdbImports, ICorDebugType **ppType);
HRESULT FindTypeModule(std::vector<std::string> &identifiers, ICorDebugThread *pThread,
                       const PDB::ImportsAndAliases &pdbImports, ICorDebugModule **ppModule);

// Get the fully-qualified "display" type name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeTypeName(ICorDebugFrame *pFrame, std::string &displayTypeName);
// Get the fully-qualified "display" method name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, std::string &displayName);

} // namespace dncdbg::EvalMetadataHelpers

#endif // DEBUGGER_EVALUATION_EVALHELPERS_METADATA_H
