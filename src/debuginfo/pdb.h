// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDB_H
#define DEBUGINFO_PDB_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/memorybuffer.h"
#include "utils/torelease.h"
#include <array>
#include <dnmd.h>
#include <cstdint>

namespace dncdbg
{

constexpr uint8_t g_pdbid_size = 20;
constexpr uint8_t g_guid_size = 16;
constexpr uint8_t g_stamp_size = 4;
// PDB ID = GUID (16 bytes) + date/time stamp (4 bytes)
using PdbIdentity = std::array<uint8_t, g_pdbid_size>;

struct PDBHolder
{
    mdhandle_t m_handle = nullptr;
    MemoryBuffer m_memBuff;
    ToRelease<ICorDebugModule> m_trModule;

    PDBHolder() = default;
    PDBHolder(mdhandle_t handle, MemoryBuffer &&memBuff, ICorDebugModule *pModule)
        : m_handle(handle),
          m_memBuff(std::move(memBuff)),
          m_trModule(pModule)
    {
    }

    PDBHolder(PDBHolder &&other) noexcept
        : m_handle(other.m_handle),
          m_memBuff(std::move(other.m_memBuff)),
          m_trModule(std::move(other.m_trModule))
    {
        other.m_handle = nullptr;
    }

    PDBHolder(const PDBHolder &) = delete;
    PDBHolder &operator=(PDBHolder &&) = delete;
    PDBHolder &operator=(const PDBHolder &) = delete;

    ~PDBHolder()
    {
        if (m_handle != nullptr)
        {
            md_destroy_handle(m_handle);
        }
    }
};

} // namespace dncdbg

#endif // DEBUGINFO_PDB_H
