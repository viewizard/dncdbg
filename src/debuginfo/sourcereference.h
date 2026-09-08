// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_SOURCEREFERENCE_H
#define DEBUGINFO_SOURCEREFERENCE_H

#include "debuginfo/pdb.h"
#include "types/protocol.h"
#include <mutex>
#include <string>
#include <vector>

namespace dncdbg
{

class SourceReference
{
  public:

    static HRESULT GetGlobalIndex(int32_t sourceReference, PDB::GlobalFileIndex &globalIndex);
    static HRESULT GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference,
                                      std::string &correctSourceFilePath);

    static std::vector<Source> LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress);
    static std::vector<Source> UnloadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress);
    static void Cleanup();

  private:

    static int32_t m_sourceReferenceCount;

    static std::unordered_map<PDB::GlobalFileIndex, int32_t, PDB::GlobalFileIndexHash> &GetGlobalIndexMap()
    {
        static std::unordered_map<PDB::GlobalFileIndex, int32_t, PDB::GlobalFileIndexHash> globalIndexMap;
        return globalIndexMap;
    }

    static std::unordered_map<int32_t, PDB::GlobalFileIndex> &GetSourceReferenceMap()
    {
        static std::unordered_map<int32_t, PDB::GlobalFileIndex> sourceReferenceMap;
        return sourceReferenceMap;
    }

    static std::mutex &GetSourceReferenceMutex()
    {
        static std::mutex sourceReferenceMutex;
        return sourceReferenceMutex;
    }
};

} // namespace dncdbg

#endif // DEBUGINFO_SOURCEREFERENCE_H
