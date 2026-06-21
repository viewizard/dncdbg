// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDBREADER_H
#define DEBUGINFO_PDBREADER_H

#include "debuginfo/pdb.h"
#include "utils/utf.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg
{

class PDBReader
{
  public:

    struct MethodRange
    {
        mdMethodDef methodDef{mdMethodDefNil};
        int32_t startLine{0};   // first segment/method SequencePoint's startLine
        int32_t endLine{0};     // last segment/method SequencePoint's endLine
        int32_t startColumn{0}; // first segment/method SequencePoint's startColumn
        int32_t endColumn{0};   // last segment/method SequencePoint's endColumn
        bool isCtor{false};     // whether method data is constructor-related

        MethodRange(mdMethodDef methodDef_,
                   int32_t startLine_,
                   int32_t endLine_,
                   int32_t startColumn_,
                   int32_t endColumn_,
                   bool isCtor_)
            : methodDef(methodDef_),
              startLine(startLine_),
              endLine(endLine_),
              startColumn(startColumn_),
              endColumn(endColumn_),
              isCtor(isCtor_)
        {
        }

        bool operator<(const MethodRange &other) const
        {
            return endLine < other.endLine || (endLine == other.endLine && endColumn < other.endColumn);
        }

        bool operator<(const int32_t lineNum) const
        {
            return endLine < lineNum;
        }

        bool operator==(const MethodRange &other) const
        {
            return methodDef == other.methodDef && startLine == other.startLine && endLine == other.endLine &&
                startColumn == other.startColumn && endColumn == other.endColumn;
        }

        [[nodiscard]] bool NestedInto(const MethodRange &other) const
        {
            return (startLine > other.startLine || (startLine == other.startLine && startColumn >= other.startColumn)) &&
                (endLine < other.endLine || (endLine == other.endLine && endColumn <= other.endColumn));
        }
    };

    struct LocalConstant
    {
        WSTRING name;                   // constant name in UTF-16 encoding
        std::vector<uint8_t> signature; // constant signature blob from PDB
    };

    static HRESULT OpenPDB(const std::string &pdbPath, const PdbIdentity &pdbId, PDBHolder &pdbHolder);
    static HRESULT GetAllSourceFiles(mdhandle_t pdbHandle, std::vector<std::string> &sourceFiles);
    static HRESULT GetMethodsRanges(mdhandle_t pdbHandle, const std::unordered_set<mdMethodDef> &constrTokens,
                                    std::unordered_map<uint32_t, std::vector<MethodRange>> &srcMethodsMap);
    static HRESULT GetLocalConstants(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                     std::vector<LocalConstant> &localConsts);
    static HRESULT GetLocalVariableName(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                        uint32_t localVarIndex, WSTRING &localVarName);
    static bool IsHoistedLocalInScope(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex);

};

} // namespace dncdbg

#endif // DEBUGINFO_PDBREADER_H
