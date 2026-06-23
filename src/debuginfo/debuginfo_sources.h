// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_DEBUGINFO_SOURCES_H
#define DEBUGINFO_DEBUGINFO_SOURCES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "managed/interop.h"
#include "utils/torelease.h"
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace dncdbg
{

using ResolveFunctionBreakpointCallback = std::function<HRESULT(ICorDebugModule *, mdMethodDef &)>;

struct file_data_t
{
    uint32_t fullPathIndex{0};
    std::vector<Interop::method_data_t> methodsData;

    file_data_t(uint32_t fullPathIndex_, std::vector<Interop::method_data_t> &&methodsData_) noexcept
        : fullPathIndex(fullPathIndex_),
          methodsData(std::move(methodsData_))
    {
    }

    file_data_t(file_data_t &&other) noexcept
        : fullPathIndex(other.fullPathIndex),
          methodsData(std::move(other.methodsData))
    {
    }

    file_data_t() = default;
    file_data_t(const file_data_t &) = delete;
    file_data_t &operator=(file_data_t &&) = delete;
    file_data_t &operator=(const file_data_t &) = delete;
    ~file_data_t() = default;
};

struct method_data_t_hash
{
    size_t operator()(const Interop::method_data_t &p) const
    {
        // Hash combining based on boost::hash_combine
        // Golden ratio constant for better bit distribution
        static constexpr size_t hash_golden_ratio = 0x9e3779b9;
        static constexpr size_t hash_shift_bits = 6;

        size_t seed = 0;
        const auto hashCombine =
            [&seed](const auto &value)
            {
                const std::hash<std::decay_t<decltype(value)>> hasher;
                seed ^= hasher(value) + hash_golden_ratio + (seed << hash_shift_bits) + (seed >> hash_shift_bits);
            };

        hashCombine(p.methodDef);
        hashCombine(p.startLine);
        hashCombine(p.endLine);
        hashCombine(p.startColumn);
        hashCombine(p.endColumn);

        return seed;
    }
};

class DebugInfo;

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

        resolved_bp_t(int32_t startLine_,
                      int32_t endLine_,
                      uint32_t ilOffset_,
                      uint32_t methodToken_,
                      ICorDebugModule *pModule)
            : startLine(startLine_),
              endLine(endLine_),
              ilOffset(ilOffset_),
              methodToken(methodToken_),
              trModule(pModule)
        {
        }
    };

    HRESULT ResolveBreakpoint(DebugInfo *pDebugInfo, CORDB_ADDRESS modAddress, const std::string &filename, int sourceLine,
                              uint32_t &fullname_index, std::vector<resolved_bp_t> &resolvedPoints);

    HRESULT FillSourcesCodeLinesForModule(ICorDebugModule *pModule, IMetaDataImport *pMDImport, void *pSymbolReaderHandle);
    HRESULT GetSourceFullPathByIndex(uint32_t index, std::string &fullPath);
    HRESULT GetIndexBySourceFullPath(const std::string &fullPath, uint32_t &index);

  private:

    struct FileMethodsData
    {
        CORDB_ADDRESS modAddress = 0;
        // properly ordered arrays of method data on each nested level
        std::vector<std::vector<Interop::method_data_t>> methodsData;
    };

    // Note, breakpoints setup and running the debuggee process could happen at the same time.
    std::mutex m_sourcesInfoMutex;
    // Note, we only add to m_sourceIndexToPath/m_sourcePathToIndex/m_sourceIndexToInitialFullPath, "size()" is used as
    // the index in the map when a new element is added. m_sourceIndexToPath - mapping index to full path
    std::vector<std::string> m_sourceIndexToPath;
    // m_sourcePathToIndex - mapping full path to index
    std::unordered_map<std::string, uint32_t> m_sourcePathToIndex;
    // m_sourceNameToFullPathsIndexes - mapping file name to set of paths with this file name
    std::unordered_map<std::string, std::set<uint32_t>> m_sourceNameToFullPathsIndexes;
    // m_sourcesMethodsData - all method data indexed by full path, the second vector holds data with the same full path for different modules,
    //                        since we may have modules with the same source full path
    std::vector<std::vector<FileMethodsData>> m_sourcesMethodsData;

    HRESULT GetFullPathIndex(BSTR document, uint32_t &fullPathIndex);
    HRESULT ResolveRelativeSourceFileName(std::string &filename);
    HRESULT GetPdbMethodsRanges(IMetaDataImport *pMDImport, void *pSymbolReaderHandle, std::vector<file_data_t> &inputData);

    // Source file names could be converted to uppercase in containers above or changed by SourceFileMap,
    // this vector holds initial full path names
    std::vector<std::string> m_sourceIndexToInitialFullPath;
};

} // namespace dncdbg

#endif // DEBUGINFO_DEBUGINFO_SOURCES_H
