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

#include <list>
#include <string>
#include <vector>

namespace dncdbg
{

class DebugInfo;

namespace MetadataHelpers
{

// TODO: Fix all this mess with names, use:
// "metadata" prefix, for example "metadataTypeName", for metadata/CLR-related names, for example "MyNamespace.Class1`2+NestedClass`1"
// "display" prefix, for example "displayTypeName", for display-related names, for example "MyNamespace.Class1<string,int>.NestedClass<int>"
//                                                  or "MyNamespace.Class1<,>.NestedClass<>" in case generic types are not available

// Get fully-qualified metadata (FQMD) name.
HRESULT GetFQMDNameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &metadataName);
// Get fully-qualified metadata (FQMD) name.
HRESULT GetFQMDNameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &metadataName);

HRESULT NameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &mdName,
                       std::list<std::string> *args);
HRESULT NameForToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, bool bClassName,
                     std::list<std::string> *args);
HRESULT NameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, std::list<std::string> *args);
HRESULT NameForTypeByType(ICorDebugType *pType, std::string &mdName);
HRESULT NameForTypeByValue(ICorDebugValue *pValue, std::string &mdName);
HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &output);
HRESULT GetTypeOfValue(ICorDebugValue *pValue, std::string &output);
HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &elementType, std::string &arrayType);
HRESULT GetTypeAndMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &typeName, std::string &methodName);
HRESULT GetTypeAndMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &typeName, std::string &methodName);
HRESULT GetFullyQualifiedMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &output);
HRESULT GetFullyQualifiedMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &output);

// Parse generic type/method arguments from a "display" type/method name (e.g. "Dictionary<int, string>").
// Returns the vector of generic argument "display" names and writes the "metadata" name (e.g. "Dictionary`2") to "metadataName".
std::vector<std::string> ConvertDisplayToMetadataName(const std::string &displayName, std::string &metadataName);
// Split a fully-qualified (FQ) "displayTypeName" into dot-separated "display" identifier components
// (namespace/class path); array ranks encountered are appended to "ranks".
std::vector<std::string> SplitFQDisplayTypeName(const std::string &displayTypeName, std::vector<int> &ranks);

// Note: `identifiers` contain display names and are converted into metadata names for lookup inside method logic.
HRESULT FindType(const std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, ICorDebugType **ppType);
HRESULT FindTypeModule(const std::vector<std::string> &identifiers, ICorDebugThread *pThread, ICorDebugModule **ppModule);

} // namespace MetadataHelpers

} // namespace dncdbg

#endif // METADATA_HELPERS_H
