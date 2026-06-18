// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDBREADER_H
#define DEBUGINFO_PDBREADER_H

#include "debuginfo/pdb.h"
#include <string>
#include <vector>

namespace dncdbg
{

class PDBReader
{
  public:

    static HRESULT OpenPDB(const std::string &pdbPath, const PdbIdentity &pdbId, PDBHolder &pdbHolder);
    static HRESULT GetAllSourceFiles(mdhandle_t pdbHandle, std::vector<std::string> &sourceFiles);

};

} // namespace dncdbg

#endif // DEBUGINFO_PDBREADER_H
