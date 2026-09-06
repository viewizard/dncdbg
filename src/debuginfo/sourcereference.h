// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_SOURCEREFERENCE_H
#define DEBUGINFO_SOURCEREFERENCE_H

#include "debuginfo/pdb.h"
#include <mutex>

namespace dncdbg
{

class SourceReference
{
  public:

    static HRESULT GetGlobalIndex(int32_t sourceReference, PDB::GlobalFileIndex &globalIndex);
    static HRESULT GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference);

    static void LoadModule();
    static void ManagedCallbackUnloadModule(CORDB_ADDRESS baseAddress);
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
