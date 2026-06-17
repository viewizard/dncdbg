// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/pdbreader.h"
#include <dnmd_pdb.h>

namespace dncdbg
{

HRESULT PDBReader::OpenPDB(const std::string &pdbPath, const PdbIdentity &pdbId, PDBHolder &pdbHolder)
{
    MemoryBuffer mBuff;
    if (!mBuff.Open(pdbPath))
    {
        return E_FAIL;
    }

    mdhandle_t handle = nullptr;
    if (!md_create_handle(mBuff.Data(), static_cast<uint32_t>(mBuff.Size()), &handle))
    {
        return E_FAIL;
    }

    std::array<uint8_t, g_pdbid_size> rawPdbId{};
    size_t size = g_pdbid_size;
    if (!md_get_pdb_id(handle, &size, rawPdbId.data()))
    {
        md_destroy_handle(handle);
        return E_FAIL;
    }

    if (rawPdbId != pdbId)
    {
        md_destroy_handle(handle);
        return E_FAIL;
    }

    // Destroy any existing handle before assigning a new one
    if (pdbHolder.m_handle != nullptr)
    {
        md_destroy_handle(pdbHolder.m_handle);
    }
    pdbHolder.m_handle = handle;
    pdbHolder.m_memBuff = std::move(mBuff);
    return S_OK;
}

} // namespace dncdbg
