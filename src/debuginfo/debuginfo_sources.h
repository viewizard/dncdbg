// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "utils/torelease.h"
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include <string>

namespace dncdbg
{

using ResolveFunctionBreakpointCallback = std::function<HRESULT(ICorDebugModule *, mdMethodDef &)>;

struct method_data_t
{
    mdMethodDef methodDef;
    int32_t startLine;   // first segment/method SequencePoint's startLine
    int32_t endLine;     // last segment/method SequencePoint's endLine
    int32_t startColumn; // first segment/method SequencePoint's startColumn
    int32_t endColumn;   // last segment/method SequencePoint's endColumn
    int32_t isCtor;      // is method data constructor related

    method_data_t()
      : methodDef(0),
        startLine(0),
        endLine(0),
        startColumn(0),
        endColumn(0),
        isCtor(0)
    {
    }

    method_data_t(mdMethodDef methodDef_, int32_t startLine_, int32_t endLine_, int32_t startColumn_, int32_t endColumn_, int32_t isCtor_)
        : methodDef(methodDef_),
          startLine(startLine_),
          endLine(endLine_),
          startColumn(startColumn_),
          endColumn(endColumn_),
          isCtor(isCtor_)
    {
    }

    bool operator<(const method_data_t &other) const
    {
        return endLine < other.endLine || (endLine == other.endLine && endColumn < other.endColumn);
    }

    bool operator<(const int32_t lineNum) const
    {
        return endLine < lineNum;
    }

    bool operator==(const method_data_t &other) const
    {
        return methodDef == other.methodDef && startLine == other.startLine && endLine == other.endLine &&
               startColumn == other.startColumn && endColumn == other.endColumn;
    }

    bool NestedInto(const method_data_t &other) const
    {
        return (startLine > other.startLine || (startLine == other.startLine && startColumn >= other.startColumn)) &&
               (endLine < other.endLine || (endLine == other.endLine && endColumn <= other.endColumn));
    }
};

struct method_data_t_hash
{
    size_t operator()(const method_data_t &p) const
    {
        return (size_t)p.methodDef + (size_t)p.startLine * 100 + (size_t)p.endLine * 1000;
    }
};

class DebugInfo;
struct PDBInfo;

class DebugInfoSources
{
  public:

    struct resolved_bp_t
    {
        int32_t startLine;
        int32_t endLine;
        uint32_t ilOffset;
        uint32_t methodToken;
        ToRelease<ICorDebugModule> trModule;

        resolved_bp_t(int32_t startLine_, int32_t endLine_, uint32_t ilOffset_, uint32_t methodToken_, ICorDebugModule *pModule)
            : startLine(startLine_),
              endLine(endLine_),
              ilOffset(ilOffset_),
              methodToken(methodToken_),
              trModule(pModule)
        {
        }
    };

    HRESULT ResolveBreakpoint(
        /*in*/ DebugInfo *pDebugInfo,
        /*in*/ CORDB_ADDRESS modAddress,
        /*in*/ const std::string &filename,
        /*out*/ unsigned &fullname_index,
        /*in*/ int sourceLine,
        /*out*/ std::vector<resolved_bp_t> &resolvedPoints);

    HRESULT FillSourcesCodeLinesForModule(ICorDebugModule *pModule, IMetaDataImport *pMDImport, void *pSymbolReaderHandle);
    HRESULT GetSourceFullPathByIndex(unsigned index, std::string &fullPath);
    HRESULT GetIndexBySourceFullPath(const std::string &fullPath, unsigned &index);

  private:

    struct FileMethodsData
    {
        CORDB_ADDRESS modAddress = 0;
        // properly ordered on each nested level arrays of methods data
        std::vector<std::vector<method_data_t>> methodsData;
    };

    // Note, breakpoints setup and ran debuggee's process could be in the same time.
    std::mutex m_sourcesInfoMutex;
    // Note, we only add to m_sourceIndexToPath/m_sourcePathToIndex/m_sourceIndexToInitialFullPath, "size()" used as
    // index in map at new element add. m_sourceIndexToPath - mapping index to full path
    std::vector<std::string> m_sourceIndexToPath;
    // m_sourcePathToIndex - mapping full path to index
    std::unordered_map<std::string, unsigned> m_sourcePathToIndex;
    // m_sourceNameToFullPathsIndexes - mapping file name to set of paths with this file name
    std::unordered_map<std::string, std::set<unsigned>> m_sourceNameToFullPathsIndexes;
    // m_sourcesMethodsData - all methods data indexed by full path, second vector hold data with same full path for different modules,
    //                        since we may have modules with same source full path
    std::vector<std::vector<FileMethodsData>> m_sourcesMethodsData;

    HRESULT GetFullPathIndex(BSTR document, unsigned &fullPathIndex);
    HRESULT ResolveRelativeSourceFileName(std::string &filename);

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    // all files names converted to uppercase in containers above, but this vector hold initial full path names
    std::vector<std::string> m_sourceIndexToInitialFullPath;
#endif
};

} // namespace dncdbg
