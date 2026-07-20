// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_TYPEPRINTER_H
#define METADATA_TYPEPRINTER_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <list>
#include <string>

namespace dncdbg
{
class DebugInfo;
} // namespace dncdbg

namespace dncdbg::TypePrinter
{

// TODO: Fix all this mess with names, use:
// "metadata" prefix, for example "metadataTypeName", for metadata/CLR-related names, for example "MyNamespace.Class1`2+NestedClass`1"
// "display" prefix, for example "displayTypeName", for display-related names, for example "MyNamespace.Class1<string,int>.NestedClass<int>"
//                                                  or "MyNamespace.Class1<,>.NestedClass<>" in case generic types are not available

HRESULT FullyQualifiedNameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &mdName);
HRESULT FullyQualifiedNameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName);
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
std::string RenameToSystem(const std::string &typeName);
std::string RenameToCSharp(const std::string &typeName);

} // namespace dncdbg::TypePrinter

#endif // METADATA_TYPEPRINTER_H
