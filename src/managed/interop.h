// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#ifndef MANAGED_INTEROP_H
#define MANAGED_INTEROP_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "utils/platform.h"
#include "utils/utf.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace dncdbg::Interop
{

struct SequencePoint
{
    int32_t startLine{0};
    int32_t startColumn{0};
    int32_t endLine{0};
    int32_t endColumn{0};
    int32_t offset{0};
    BSTR document{nullptr};

    SequencePoint() = default;
    ~SequencePoint() noexcept;

    SequencePoint(const SequencePoint &) = delete;
    SequencePoint &operator=(const SequencePoint &) = delete;
    SequencePoint(SequencePoint &&other) noexcept
        : startLine(other.startLine),
          startColumn(other.startColumn),
          endLine(other.endLine),
          endColumn(other.endColumn),
          offset(other.offset),
          document(other.document)
    {
        other.document = nullptr;
    }
    SequencePoint &operator=(SequencePoint &&other) noexcept
    {
        if (this == std::addressof(other))
        {
            return *this;
        }
        startLine = other.startLine;
        startColumn = other.startColumn;
        endLine = other.endLine;
        endColumn = other.endColumn;
        offset = other.offset;
        document = other.document;
        other.document = nullptr;
        return *this;
    }
};

struct AsyncAwaitInfoBlock
{
    uint32_t yield_offset{0};
    uint32_t resume_offset{0};
    uint32_t token{0}; // note, this is internal token number, runtime method token for module should be calculated as "mdMethodDefNil + token"
};

struct LocalConstantInfo
{
    BSTR name{nullptr};
    uint8_t *signature{nullptr};
    int32_t signatureSize{0};
};

// WARNING! Due to CoreCLR limitations, Init() / Shutdown() sequence can be used only once during process execution.
// Note, init in case of error will throw exception, since this is fatal for debugger (CoreCLR can't be re-init).
void Init(const std::string &coreClrPath);
// WARNING! Due to CoreCLR limitations, Shutdown() can't be called out of the Main() scope, for example, from global object destructor.
void Shutdown();

HRESULT LoadSymbolsForPortablePDB(const std::string &modulePath, BOOL isInMemory, BOOL isFileLayout, uint64_t peAddress, uint64_t peSize,
                                  uint64_t inMemoryPdbAddress, uint64_t inMemoryPdbSize, void **ppSymbolReaderHandle, std::string &pdbPath);
void DisposeSymbols(void *pSymbolReaderHandle);
HRESULT GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef MethodToken, uint32_t IlOffset, SequencePoint *sequencePoint);
HRESULT GetNextUserCodeILOffset(void *pSymbolReaderHandle, mdMethodDef MethodToken, uint32_t IlOffset,
                                uint32_t &ilNextOffset, bool *noUserCodeFound);
HRESULT GetNamedLocalVariableAndScope(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t localIndex,
                                      WSTRING &localName, int32_t *pIlStart, int32_t *pIlEnd);
HRESULT GetHoistedLocalScopes(void *pSymbolReaderHandle, mdMethodDef methodToken, void **data, int32_t &hoistedLocalScopesCount);
HRESULT GetStepRangesFromIP(void *pSymbolReaderHandle, uint32_t ip, mdMethodDef MethodToken, uint32_t *ilStartOffset, uint32_t *ilEndOffset);
HRESULT GetModuleMethodsRanges(void *pSymbolReaderHandle, uint32_t constrTokensNum, void *constrTokens,
                               uint32_t normalTokensNum, void *normalTokens, void **data);
HRESULT ResolveBreakPoints(void *pSymbolReaderHandles, int32_t tokenNum, void *Tokens, int32_t sourceLine,
                           int32_t nestedToken, int32_t &Count, const std::string &sourcePath, void **data);
HRESULT GetAsyncMethodSteppingInfo(void *pSymbolReaderHandle, mdMethodDef methodToken,
                                   std::vector<AsyncAwaitInfoBlock> &AsyncAwaitInfo, uint32_t *ilOffset);
HRESULT GetLocalConstants(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                          void **data, int32_t &constantCount);
HRESULT Calculation(void *firstOp, int32_t firstType, void *secondOp, int32_t secondType, int32_t operationType,
                    int32_t &resultType, void **data, std::string &errorText);
HRESULT GenerateStackMachineProgram(const std::string &expr, void **ppStackProgram, std::string &textOutput);
void ReleaseStackMachineProgram(void *pStackProgram);
HRESULT NextStackCommand(void *pStackProgram, int32_t &Command, void **Ptr, std::string &textOutput);
void *AllocString(const std::string &str);
HRESULT StringToUpper(std::string &String);
BSTR SysAllocStringLen(int32_t size);
void SysFreeString(BSTR ptrBSTR);
void CoTaskMemFree(void *ptr);

} // namespace dncdbg::Interop

#endif // MANAGED_INTEROP_H
