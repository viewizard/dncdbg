// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_SOURCEREFERENCE_H
#define DEBUGINFO_SOURCEREFERENCE_H

#include "debuginfo/pdb.h"
#include "types/protocol.h"
#include <string>
#include <vector>

namespace dncdbg::SourceReference
{

HRESULT GetGlobalIndex(int32_t sourceReference, PDB::GlobalFileIndex &globalIndex);
HRESULT GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference, std::string &correctSourceFilePath);
HRESULT GetSourceURL(const PDB::GlobalFileIndex &globalIndex, std::string &url);
void AddLoadedSourcesForModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress, std::vector<Source> &sources);

std::vector<Source> LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress);
std::vector<Source> UnloadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress);
void Cleanup();

} // namespace dncdbg::SourceReference

#endif // DEBUGINFO_SOURCEREFERENCE_H
