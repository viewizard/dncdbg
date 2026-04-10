// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debuginfo_sources.h"
#include "debuginfo/debuginfo.h"
#include "utils/filesystem.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <memory>

namespace dncdbg
{

namespace
{

// Note, we use std::map since we need container that will not invalidate iterators on add new elements.
void AddMethodData(/*in,out*/ std::map<size_t, std::set<Interop::method_data_t>> &methodData,
                   const Interop::method_data_t &entry, const size_t level)
{
    struct MethodDataWithLevel
    {
        MethodDataWithLevel(const Interop::method_data_t &entry_, const size_t level_)
            : entry(entry_),
              level(level_)
        {
        }

        Interop::method_data_t entry;
        size_t level;
    };

    std::list<MethodDataWithLevel> methodDataQueue;
    methodDataQueue.emplace_back(entry, level);

    while (!methodDataQueue.empty())
    {
        MethodDataWithLevel currentData = methodDataQueue.front();
        methodDataQueue.pop_front();

        // if we are here, we need at least one nested level for sure
        if (methodData.empty())
        {
            methodData.emplace(0, std::set<Interop::method_data_t>{currentData.entry});
            continue;
        }
        assert(currentData.level <= methodData.size()); // could be increased only at 1 per recursive call
        if (currentData.level == methodData.size())
        {
            methodData.emplace(currentData.level, std::set<Interop::method_data_t>{currentData.entry});
            continue;
        }

        auto &levelMethodData = methodData.at(currentData.level);

        auto it = levelMethodData.lower_bound(currentData.entry);
        if (it != levelMethodData.end() && currentData.entry.NestedInto(*it))
        {
            methodDataQueue.emplace_back(currentData.entry, currentData.level + 1);
            continue;
        }

        // case with only one element on nested level, NestedInto() was already called and entry checked
        if (it == levelMethodData.begin())
        {
            levelMethodData.emplace(currentData.entry);
            continue;
        }

        // in case these are parts of constructor with same location (for example, `int i = 0;`)
        if (it != levelMethodData.end() && *it == currentData.entry && currentData.entry.isCtor == 1)
        {
            assert(it->isCtor == 1); // also must be part of constructor
            levelMethodData.emplace(currentData.entry);
            continue;
        }

        // move all previously added nested elements for the new entry to the level above
        while (it != levelMethodData.begin())
        {
            it = std::prev(it);

            if (it->NestedInto(currentData.entry))
            {
                const Interop::method_data_t tmp = *it;
                it = levelMethodData.erase(it);
                methodDataQueue.emplace_back(tmp, currentData.level + 1);
            }
            else
            {
                break;
            }
        };

        levelMethodData.emplace(currentData.entry);
    }
}

bool GetMethodTokensByLineNumber(const std::vector<std::vector<Interop::method_data_t>> &methodBpData,
                                 /*in,out*/ int32_t &lineNum,
                                 /*out*/ std::vector<mdMethodDef> &Tokens,
                                 /*out*/ mdMethodDef &closestNestedToken)
{
    const Interop::method_data_t *result = nullptr;
    closestNestedToken = 0;

    for (auto it = methodBpData.cbegin(); it != methodBpData.cend(); ++it)
    {
        auto lower = std::lower_bound(it->cbegin(), it->cend(), lineNum);
        if (lower == it->cend())
        {
            break; // point behind last method for this nested level
        }

        // case with first line of method, for example:
        // void Method(){
        //            void Method(){ void Method(){...  <- breakpoint at this line
        if (lineNum == lower->startLine)
        {
            // At this point we can't check this case, let managed part decide (since it see Columns):
            // void Method() {
            // ... code ...; void Method() {     <- breakpoint at this line
            //  };
            if (result != nullptr)
            {
                if (lower->isCtor == 1) // part of constructor
                {
                    assert(result->isCtor == 1); // also must be part of constructor
                    Tokens.emplace_back(result->methodDef);
                    result = &(*lower);
                    continue; // need check nested level (if available)
                }

                closestNestedToken = lower->methodDef;
            }
            else
            {
                result = &(*lower);

                if (lower->isCtor == 1) // part of constructor
                {
                    continue; // need check nested level (if available)
                }
            }
        }
        else if (lineNum > lower->startLine && lower->endLine >= lineNum)
        {
            if (result != nullptr && lower->isCtor == 1) // part of constructor
            {
                assert(result->isCtor == 1); // also must be part of constructor
                Tokens.emplace_back(result->methodDef);
            }

            result = &(*lower);
            continue; // need check nested level (if available)
        }
        // out of first-level method lines - forced move line to first method below, for example:
        //  <-- breakpoint at line without code (out of any methods)
        // void Method() {...}
        else if (it == methodBpData.cbegin() && lineNum < lower->startLine)
        {
            lineNum = lower->startLine;
            result = &(*lower);

            if (lower->isCtor == 1) // part of constructor
            {
                continue; // need check nested level (if available)
            }
        }
        // result was found on previous loop, check for closest nested method
        // need it in case of breakpoint setup at lines without code and before nested method, for example:
        // {
        //  <-- breakpoint at line without code (inside method)
        //     void Method() {...}
        // }
        else if (result != nullptr && lineNum <= lower->startLine && lower->endLine <= result->endLine)
        {
            closestNestedToken = lower->methodDef;
        }

        break;
    }

    if (result != nullptr)
    {
        Tokens.emplace_back(result->methodDef);
    }

    return (result != nullptr);
}

} // unnamed namespace

HRESULT DebugInfoSources::GetPdbMethodsRanges(IMetaDataImport *pMDImport, void *pSymbolReaderHandle,
                                              std::unordered_set<mdMethodDef> *methodTokens,
                                              std::vector<file_data_t> &inputData)
{
    HRESULT Status = S_OK;
    // Note, we need 2 arrays of tokens - for normal methods and constructors (.ctor/.cctor, that could have segmented code).
    std::vector<int32_t> constrTokens;
    std::vector<int32_t> normalTokens;

    ULONG numTypedefs = 0;
    HCORENUM hEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    while (SUCCEEDED(pMDImport->EnumTypeDefs(&hEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0)
    {
        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(pMDImport->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            if ((methodTokens != nullptr) && methodTokens->find(methodDef) == methodTokens->end())
            {
                continue;
            }

            ULONG funcNameLen = 0;
            if (FAILED(pMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &funcNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            WSTRING funcName(funcNameLen - 1, '\0'); // funcNameLen - string size + null terminated symbol
            if (FAILED(pMDImport->GetMethodProps(methodDef, nullptr, funcName.data(), funcNameLen, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (funcName == W(".ctor") ||
                funcName == W(".cctor"))
            {
                constrTokens.emplace_back(methodDef);
            }
            else
            {
                normalTokens.emplace_back(methodDef);
            }
        }
        pMDImport->CloseEnum(fEnum);
    }
    pMDImport->CloseEnum(hEnum);

    if (sizeof(std::size_t) > sizeof(uint32_t) && (constrTokens.size() > std::numeric_limits<uint32_t>::max() ||
                                                   normalTokens.size() > std::numeric_limits<uint32_t>::max()))
    {
        LOGE(log << "Too big token arrays.");
        return E_FAIL;
    }

    void *data = nullptr;
    IfFailRet(Interop::GetModuleMethodsRanges(pSymbolReaderHandle, static_cast<uint32_t>(constrTokens.size()), constrTokens.data(),
                                              static_cast<uint32_t>(normalTokens.size()), normalTokens.data(), &data));
    if (data == nullptr)
    {
        return S_OK;
    }

    struct module_methods_data_t_deleter
    {
        void operator()(Interop::module_methods_data_t *data) const
        {
            if (data->moduleMethodsData == nullptr)
            {
                return;
            }

            auto *curMethodsData = static_cast<Interop::file_methods_data_t *>(data->moduleMethodsData);
            const Interop::file_methods_data_t *endMethodsData = curMethodsData + data->fileNum;

            while (curMethodsData != endMethodsData)
            {
                if (curMethodsData->document != nullptr)
                {
                    Interop::SysFreeString(curMethodsData->document);
                }
                if (curMethodsData->methodsData != nullptr)
                {
                    Interop::CoTaskMemFree(curMethodsData->methodsData);
                }
                ++curMethodsData;
            }

            Interop::CoTaskMemFree(data->moduleMethodsData);
            Interop::CoTaskMemFree(data);
        }
    };

    std::unique_ptr<Interop::module_methods_data_t, module_methods_data_t_deleter> rawData(static_cast<Interop::module_methods_data_t *>(data));

    if (rawData->moduleMethodsData == nullptr ||
        rawData->fileNum == 0)
    {
        return S_OK;
    }

    auto *curMethodsData = static_cast<Interop::file_methods_data_t *>(rawData->moduleMethodsData);
    const Interop::file_methods_data_t *endMethodsData = curMethodsData + rawData->fileNum;
    inputData.reserve(rawData->fileNum);

    while (curMethodsData != endMethodsData)
    {
        unsigned fullPathIndex = 0;
        if (curMethodsData->document == nullptr ||
            curMethodsData->methodsData == nullptr ||
            FAILED(GetFullPathIndex(curMethodsData->document, fullPathIndex)))
        {
            ++curMethodsData;
            continue;
        }

        std::vector<Interop::method_data_t> methodsData;
        methodsData.resize(curMethodsData->methodNum);
        std::memcpy(methodsData.data(),
                    curMethodsData->methodsData,
                    curMethodsData->methodNum * sizeof(Interop::method_data_t));

        inputData.emplace_back(fullPathIndex, std::move(methodsData));

        ++curMethodsData;
    }

    return S_OK;
}

// Caller must care about m_sourcesInfoMutex.
HRESULT DebugInfoSources::GetFullPathIndex(BSTR document, unsigned &fullPathIndex)
{
    std::string fullPath = to_utf8(document);
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    HRESULT Status = S_OK;
    const std::string initialFullPath = fullPath;
    IfFailRet(Interop::StringToUpper(fullPath));
#endif
    auto findPathIndex = m_sourcePathToIndex.find(fullPath);
    if (findPathIndex == m_sourcePathToIndex.end())
    {
        fullPathIndex = static_cast<unsigned>(m_sourceIndexToPath.size());
        m_sourcePathToIndex.emplace(fullPath, fullPathIndex);
        m_sourceIndexToPath.emplace_back(fullPath);
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
        m_sourceIndexToInitialFullPath.emplace_back(initialFullPath);
#endif
        m_sourceNameToFullPathsIndexes[GetFileName(fullPath)].emplace(fullPathIndex);
        m_sourcesMethodsData.emplace_back();
    }
    else
    {
        fullPathIndex = findPathIndex->second;
    }

    return S_OK;
}

HRESULT DebugInfoSources::FillSourcesCodeLinesForModule(ICorDebugModule *pModule, IMetaDataImport *pMDImport,
                                                      void *pSymbolReaderHandle)
{
    const std::scoped_lock<std::mutex> lock(m_sourcesInfoMutex);

    HRESULT Status = S_OK;
    std::vector<file_data_t> inputData;
    IfFailRet(GetPdbMethodsRanges(pMDImport, pSymbolReaderHandle, nullptr, inputData));
    if (inputData.empty())
    {
        return S_OK;
    }

    // Usually, modules provide files with unique full paths for sources.
    m_sourceIndexToPath.reserve(m_sourceIndexToPath.size() + inputData.size());
    m_sourcesMethodsData.reserve(m_sourcesMethodsData.size() + inputData.size());
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    m_sourceIndexToInitialFullPath.reserve(m_sourceIndexToInitialFullPath.size() + inputData.size());
#endif

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    for (const auto &fileData : inputData)
    {
        m_sourcesMethodsData.at(fileData.fullPathIndex).emplace_back(FileMethodsData{});
        auto &fileMethodsData = m_sourcesMethodsData.at(fileData.fullPathIndex).back();
        fileMethodsData.modAddress = modAddress;

#ifdef DEBUG_INTERNAL_TESTS
        // Reorder data in reverse for testing AddMethodData() method to build proper nested levels.
        struct compare {
            bool operator()(const Interop::method_data_t &lhs, const Interop::method_data_t &rhs) const
            { return lhs.endLine < rhs.endLine || (lhs.endLine == rhs.endLine && lhs.endColumn < rhs.endColumn); }
        };
        std::multiset<Interop::method_data_t, compare> orderedInputData;
        for (const auto &methodData : fileData.methodsData)
        {
            orderedInputData.emplace(methodData);
        }
        std::map<size_t, std::set<Interop::method_data_t>> inputMethodsData;
        for (const auto &methodData : orderedInputData)
        {
            AddMethodData(inputMethodsData, methodData, 0);
        }
#else
        // Note, don't reorder input data, since it has almost ideal order for us.
        // For example, for Private.CoreLib (about 22000 methods) only 8 relocations were made.
        // In case the default methods ordering is dramatically changed, we could use data reordering.
        std::map<size_t, std::set<Interop::method_data_t>> inputMethodsData;
        for (const auto &methodData : fileData.methodsData)
        {
            AddMethodData(inputMethodsData, methodData, 0);
        }
#endif // DEBUG_INTERNAL_TESTS
        fileMethodsData.methodsData.resize(inputMethodsData.size());
        for (size_t i = 0; i < inputMethodsData.size(); i++)
        {
            fileMethodsData.methodsData.at(i).resize(inputMethodsData.at(i).size());
            std::copy(inputMethodsData.at(i).begin(), inputMethodsData.at(i).end(), fileMethodsData.methodsData.at(i).begin());
        }
    }

    m_sourcesMethodsData.shrink_to_fit();
    m_sourceIndexToPath.shrink_to_fit();
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    m_sourceIndexToInitialFullPath.shrink_to_fit();
#endif

    return S_OK;
}

HRESULT DebugInfoSources::ResolveRelativeSourceFileName(std::string &filename)
{
    // IMPORTANT! Caller should care about m_sourcesInfoMutex.
    auto findIndexesByFileName = m_sourceNameToFullPathsIndexes.find(GetFileName(filename));
    if (findIndexesByFileName == m_sourceNameToFullPathsIndexes.end())
    {
        return E_FAIL;
    }

    auto const &possiblePathsIndexes = findIndexesByFileName->second;
    std::string result = filename;

    // Care about all "./" and "../" first.
    std::list<std::string> pathDirs;
    std::size_t i = 0;
    while ((i = result.find_first_of("/\\")) != std::string::npos)
    {
        const std::string pathElement = result.substr(0, i);
        if (pathElement == "..")
        {
            if (!pathDirs.empty())
            {
                pathDirs.pop_front();
            }
        }
        else if (pathElement != ".")
        {
            pathDirs.push_front(pathElement);
        }

        result = result.substr(i + 1);
    }
    for (const auto &dir : pathDirs)
    {
        result.insert(0, dir + '/');
    }

    // The problem is - we could have several assemblies that could have same source file name with different path's
    // root. We don't really have a lot of options here, so, we assume, that all possible sources paths have same root
    // and just find the shortest.
    if (result == GetFileName(result))
    {
        auto it = std::min_element(possiblePathsIndexes.begin(), possiblePathsIndexes.end(),
            [&](const unsigned a, const unsigned b)
            {
                return m_sourceIndexToPath.at(a).size() < m_sourceIndexToPath.at(b).size();
            });

        filename = it == possiblePathsIndexes.end() ? result : m_sourceIndexToPath.at(*it);
        return S_OK;
    }

    std::list<std::string> possibleResults;
    for (const auto pathIndex : possiblePathsIndexes)
    {
        if (result.size() > m_sourceIndexToPath.at(pathIndex).size())
        {
            continue;
        }

        // Note, since assemblies could be built in different OSes, we could have different delimiters in source files
        // paths.
        auto BinaryPredicate =
            [](const char &a, const char &b)
            {
                if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
                {
                    return true;
                }
                return a == b;
            };

        // since C++17
        // if (std::equal(result.begin(), result.end(), path.end() - result.size(), BinaryPredicate))
        //    possibleResults.push_back(path);
        auto first1 = result.begin();
        auto last1 = result.end();
        auto first2 = std::prev(m_sourceIndexToPath.at(pathIndex).end(), static_cast<intptr_t>(result.size()));
        auto equal = [&]()
            {
                for (; first1 != last1; ++first1, ++first2)
                {
                    if (!BinaryPredicate(*first1, *first2))
                    {
                        return false;
                    }
                }
                return true;
            };
        if (equal())
        {
            possibleResults.push_back(m_sourceIndexToPath.at(pathIndex));
        }
    }
    // The problem is - we could have several assemblies that could have sources with same relative paths with different
    // path's root. We don't really have a lot of options here, so, we assume, that all possible sources paths have same
    // root and just find the shortest.
    if (!possibleResults.empty())
    {
        filename = possibleResults.front();
        for (const auto &path : possibleResults)
        {
            if (filename.length() > path.length())
            {
                filename = path;
            }
        }
        return S_OK;
    }

    return E_FAIL;
}

HRESULT DebugInfoSources::ResolveBreakpoint(/*in*/ DebugInfo *pDebugInfo,
                                          /*in*/ CORDB_ADDRESS modAddress,
                                          /*in*/ const std::string &filename,
                                          /*out*/ unsigned &fullname_index,
                                          /*in*/ int sourceLine,
                                          /*out*/ std::vector<resolved_bp_t> &resolvedPoints)
{
    const std::scoped_lock<std::mutex> lockSourcesInfo(m_sourcesInfoMutex);

    HRESULT Status = S_OK;
    auto findIndex = m_sourcePathToIndex.find(filename);
    if (findIndex == m_sourcePathToIndex.end())
    {
        // Check for absolute path.
#ifdef _WIN32
        // Check, if start from drive letter, for example "D:\" or "D:/".
        if (filename.size() > 2 && filename[1] == ':' && (filename[2] == '/' || filename[2] == '\\'))
#else
        if (filename.at(0) == '/')
#endif
        {
            return E_FAIL;
        }

        std::string resolvedFilename = filename;
        IfFailRet(ResolveRelativeSourceFileName(resolvedFilename));

        findIndex = m_sourcePathToIndex.find(resolvedFilename);
        if (findIndex == m_sourcePathToIndex.end())
        {
            return E_FAIL;
        }
    }

    fullname_index = findIndex->second;

    struct resolved_input_bp_t
    {
        int32_t startLine;
        int32_t endLine;
        uint32_t ilOffset;
        uint32_t methodToken;
    };

    for (const auto &sourceData : m_sourcesMethodsData.at(findIndex->second))
    {
        if ((modAddress != 0U) && modAddress != sourceData.modAddress)
        {
            continue;
        }

        std::vector<mdMethodDef> Tokens;
        int32_t correctedStartLine = sourceLine;
        mdMethodDef closestNestedToken = mdMethodDefNil;
        if (!GetMethodTokensByLineNumber(sourceData.methodsData, correctedStartLine, Tokens, closestNestedToken))
        {
            continue;
        }
        // correctedStartLine - in case line doesn't belong to any methods, if possible, will be "moved" to first line of
        // method below sourceLine.

        if (static_cast<int32_t>(Tokens.size()) > std::numeric_limits<int32_t>::max())
        {
            LOGE(log << "Too big token arrays.");
            return E_FAIL;
        }

        PDBInfo *pmdInfo = nullptr; // Note, pmdInfo must be covered by m_debugInfoMutex.
        IfFailRet(pDebugInfo->GetPDBInfo(sourceData.modAddress, &pmdInfo)); // we must have it, since we loaded data from it
        if (pmdInfo->m_symbolReaderHandle == nullptr)
        {
            continue;
        }

        void *data = nullptr;
        int32_t resolvedCount = 0;
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
        const std::string fullName = m_sourceIndexToInitialFullPath.at(findIndex->second);
#else
        const std::string fullName = m_sourceIndexToPath[findIndex->second];
#endif
        if (FAILED(Interop::ResolveBreakPoints(pmdInfo->m_symbolReaderHandle, static_cast<int32_t>(Tokens.size()), Tokens.data(),
                                               correctedStartLine, closestNestedToken, resolvedCount, fullName, &data)) ||
            data == nullptr)
        {
            continue;
        }
        if (resolvedCount <= 0)
        {
            Interop::CoTaskMemFree(data);
            continue;
        }

        std::vector<resolved_input_bp_t> inputData(resolvedCount);
        std::memcpy(inputData.data(), data, resolvedCount * sizeof(resolved_input_bp_t));
        Interop::CoTaskMemFree(data);

        for (auto &entry : inputData)
        {
            pmdInfo->m_trModule->AddRef();
            resolvedPoints.emplace_back(entry.startLine, entry.endLine, entry.ilOffset,
                                        entry.methodToken, pmdInfo->m_trModule.GetPtr());
        }
    }

    return S_OK;
}

HRESULT DebugInfoSources::GetSourceFullPathByIndex(unsigned index, std::string &fullPath)
{
    const std::scoped_lock<std::mutex> lock(m_sourcesInfoMutex);

    if (m_sourceIndexToPath.size() <= index)
    {
        return E_FAIL;
    }

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    fullPath = m_sourceIndexToInitialFullPath.at(index);
#else
    fullPath = m_sourceIndexToPath[index];
#endif

    return S_OK;
}

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
HRESULT DebugInfoSources::GetIndexBySourceFullPath(const std::string &fullPath_, unsigned &index)
#else
HRESULT DebugInfoSources::GetIndexBySourceFullPath(const std::string &fullPath, unsigned &index)
#endif
{
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    HRESULT Status = S_OK;
    std::string fullPath = fullPath_;
    IfFailRet(Interop::StringToUpper(fullPath));
#endif

    const std::scoped_lock<std::mutex> lock(m_sourcesInfoMutex);

    auto findIndex = m_sourcePathToIndex.find(fullPath);
    if (findIndex == m_sourcePathToIndex.end())
    {
        return E_FAIL;
    }

    index = findIndex->second;
    return S_OK;
}

} // namespace dncdbg
