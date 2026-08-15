// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_HELPERS_H
#define METADATA_HELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include "metadata/sigparse.h"
#include <list>
#include <string>
#include <vector>

namespace dncdbg
{

class DebugInfo;

namespace MetadataHelpers
{

// Naming conventions for type names:
// "metadata" prefix, for example "metadataTypeName", for metadata/CLR-related names, for example "MyNamespace.Class1`2+NestedClass`1"
// "display" prefix, for example "displayTypeName", for display-related names, for example "MyNamespace.Class1<string,int>.NestedClass<int>"
//                                                  or "MyNamespace.Class1<,>.NestedClass<>" in case generic types are not available

// Get fully-qualified "metadata" (FQMD) type name.
HRESULT GetFQMDTypeNameByToken(mdToken token, IMetaDataImport *pMDImport, std::string &metadataName);
// Get fully-qualified "metadata" (FQMD) type name.
HRESULT GetFQMDTypeNameByICorType(ICorDebugType *pType, std::string &metadataName);
// Get fully-qualified "metadata" (FQMD) type name.
HRESULT GetFQMDTypeNameByICorValue(ICorDebugValue *pValue, std::string &metadataName);

// Get fully-qualified "display" name for token.
HRESULT GetFQDisplayNameForToken(mdToken token, IMetaDataImport *pMDImport, std::string &displayName,
                                 std::list<std::string> *args);

// Get fully-qualified "display" type name.
HRESULT GetFQDisplayTypeName(ICorDebugType *pType, std::string &displayElemType, std::string &displayArrayType);
// Get fully-qualified "display" type name.
HRESULT GetFQDisplayTypeName(ICorDebugType *pType, std::string &displayTypeName);
// Get fully-qualified "display" type name.
HRESULT GetFQDisplayTypeName(ICorDebugValue *pValue, std::string &displayTypeName);

// Get the fully-qualified "display" type name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeTypeName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &displayTypeName);
// Get the fully-qualified "display" method name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &displayName);
// Get the fully-qualified "display" method name of the real (user) code, resolving async state-machine methods back to their kickoff method.
HRESULT GetFQDisplayRealCodeMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &displayName);

// Parse generic type/method arguments from a "display" type/method name (e.g. "Dictionary<int, string>").
// Returns the vector of generic argument "display" names and writes the "metadata" name (e.g. "Dictionary`2") to "metadataName".
std::vector<std::string> ConvertDisplayToMetadataName(const std::string &displayName, std::string &metadataName);
// Convert a "metadata" type/method name (e.g. "Dictionary`2") into a "display" name (e.g. "Dictionary<int, string>").
// When "args" is non-null, generic arguments are consumed from it to fill the "<...>" parameter list; when null,
// empty placeholders (e.g. "Dictionary<,>") are emitted for generic types whose arguments are not available.
std::string ConvertMetadataToDisplayName(const std::string &metadataName, std::list<std::string> *args);
// Split a fully-qualified (FQ) "displayTypeName" into dot-separated "display" identifier components
// (namespace/class path); array ranks encountered are appended to "ranks".
std::vector<std::string> SplitFQDisplayTypeName(const std::string &displayTypeName, std::vector<int> &ranks);

// Note: `identifiers` contain "display" names and are converted into "metadata" names for lookup inside method logic.
HRESULT FindType(std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, const PDB::ImportsAndAliases &pdbImports, ICorDebugType **ppType);
HRESULT FindTypeModule(std::vector<std::string> &identifiers, ICorDebugThread *pThread,
                       const PDB::ImportsAndAliases &pdbImports, ICorDebugModule **ppModule);

// Note: this is a heavy function, since it is forced to search for the type in all modules to detect the proper CorElementType
// and the proper "metadata" type name. It provides a result equivalent to a ParseElementType() call with the `addElementTypeName = false` parameter.
SigElementType GetSigElementTypeByDisplayTypeName(ICorDebugThread *pThread, const std::string &displayTypeName,
                                                  const PDB::ImportsAndAliases &pdbImports);

// Get generic type parameters as array of SigElementTypes.
HRESULT GetGenericTypeParameters(ICorDebugType *pType, std::vector<SigElementType> &genericTypeParameters);

// Returns the C# keyword/name for a built-in CorElementType (e.g. "void", "int", "string", "object").
// Returns E_FAIL for element types that are not built-in primitives or keywords.
HRESULT GetBuiltInTypeName(CorElementType elemType, std::string &typeName);

} // namespace MetadataHelpers

} // namespace dncdbg

#endif // METADATA_HELPERS_H
