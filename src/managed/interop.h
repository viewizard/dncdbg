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
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/rwlock.h"
#include "utils/utf.h"
#include <string>
#include <unordered_set>
#include <vector>

// part of coreclrhost.h file from runtime sources
#if defined(_WIN32) && defined(_M_IX86)
#define CORECLR_CALLING_CONVENTION __stdcall
#else
#define CORECLR_CALLING_CONVENTION
#endif

#define CORECLR_HOSTING_API(function, ...) /* NOLINT(cppcoreguidelines-macro-usage) */ \
    extern "C" int CORECLR_CALLING_CONVENTION function(__VA_ARGS__); \
    using function##_ptr = int (CORECLR_CALLING_CONVENTION *)(__VA_ARGS__)

CORECLR_HOSTING_API(coreclr_shutdown, void *hostHandle, unsigned int domainId);
CORECLR_HOSTING_API(coreclr_initialize, const char *exePath, const char *appDomainFriendlyName, int propertyCount,
                    const char **propertyKeys, const char **propertyValues, void **hostHandle, unsigned int *domainId);
CORECLR_HOSTING_API(coreclr_create_delegate, void *hostHandle, unsigned int domainId, const char *entryPointAssemblyName,
                    const char *entryPointTypeName, const char *entryPointMethodName, void **delegate);

namespace dncdbg
{

class Interop
{
  public:

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

    struct method_data_t
    {
        mdMethodDef methodDef;
        int32_t startLine;   // first segment/method SequencePoint's startLine
        int32_t endLine;     // last segment/method SequencePoint's endLine
        int32_t startColumn; // first segment/method SequencePoint's startColumn
        int32_t endColumn;   // last segment/method SequencePoint's endColumn
        int32_t isCtor;      // whether method data is constructor-related

        method_data_t()
            : methodDef(0),
              startLine(0),
              endLine(0),
              startColumn(0),
              endColumn(0),
              isCtor(0)
        {
        }

        method_data_t(mdMethodDef methodDef_,
                      int32_t startLine_,
                      int32_t endLine_,
                      int32_t startColumn_,
                      int32_t endColumn_,
                      int32_t isCtor_)
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

        [[nodiscard]] bool NestedInto(const method_data_t &other) const
        {
            return (startLine > other.startLine || (startLine == other.startLine && startColumn >= other.startColumn)) &&
                (endLine < other.endLine || (endLine == other.endLine && endColumn <= other.endColumn));
        }
    };

    struct file_methods_data_t
    {
        BSTR document;
        int32_t methodNum;
        method_data_t *methodsData;

        file_methods_data_t() = delete;
        file_methods_data_t(const file_methods_data_t &) = delete;
        file_methods_data_t &operator=(const file_methods_data_t &) = delete;
        file_methods_data_t(file_methods_data_t &&) = delete;
        file_methods_data_t &operator=(file_methods_data_t &&) = delete;
        ~file_methods_data_t() = default;
    };

    struct module_methods_data_t
    {
        int32_t fileNum;
        file_methods_data_t *moduleMethodsData;

        module_methods_data_t() = delete;
        module_methods_data_t(const module_methods_data_t &) = delete;
        module_methods_data_t &operator=(const module_methods_data_t &) = delete;
        module_methods_data_t(module_methods_data_t &&) = delete;
        module_methods_data_t &operator=(module_methods_data_t &&) = delete;
        ~module_methods_data_t() = default;
    };

    // WARNING! Due to CoreCLR limitations, Init() / Shutdown() sequence can be used only once during process execution.
    // Note, init in case of error will throw exception, since this is fatal for debugger (CoreCLR can't be re-init).
    static void Init(const std::string &coreClrPath);
    // WARNING! Due to CoreCLR limitations, Shutdown() can't be called out of the Main() scope, for example, from global object destructor.
    static void Shutdown();

    static HRESULT LoadSymbolsForPortablePDB(const std::string &modulePath, BOOL isInMemory, BOOL isFileLayout, const void *peAddress, uint64_t peSize,
                                             const void *inMemoryPdbAddress, uint64_t inMemoryPdbSize, void **ppSymbolReaderHandle, std::string &pdbPath);
    static void DisposeSymbols(void *pSymbolReaderHandle);
    static HRESULT GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef MethodToken, uint32_t IlOffset, SequencePoint *sequencePoint);
    static HRESULT GetNextUserCodeILOffset(void *pSymbolReaderHandle, mdMethodDef MethodToken, uint32_t IlOffset,
                                           uint32_t &ilNextOffset, bool *noUserCodeFound);
    static HRESULT GetNamedLocalVariableAndScope(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t localIndex,
                                                 WSTRING &localName, int32_t *pIlStart, int32_t *pIlEnd);
    static HRESULT GetHoistedLocalScopes(void *pSymbolReaderHandle, mdMethodDef methodToken, void **data, int32_t &hoistedLocalScopesCount);
    static HRESULT GetStepRangesFromIP(void *pSymbolReaderHandle, uint32_t ip, mdMethodDef MethodToken, uint32_t *ilStartOffset, uint32_t *ilEndOffset);
    static HRESULT GetModuleMethodsRanges(void *pSymbolReaderHandle, uint32_t constrTokensNum, void *constrTokens,
                                          uint32_t normalTokensNum, void *normalTokens, void **data);
    static HRESULT ResolveBreakPoints(void *pSymbolReaderHandles, int32_t tokenNum, void *Tokens, int32_t sourceLine,
                                      int32_t nestedToken, int32_t &Count, const std::string &sourcePath, void **data);
    static HRESULT GetAsyncMethodSteppingInfo(void *pSymbolReaderHandle, mdMethodDef methodToken,
                                              std::vector<AsyncAwaitInfoBlock> &AsyncAwaitInfo, uint32_t *ilOffset);
    static HRESULT GetLocalConstants(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                     void **data, int32_t &constantCount);
    static HRESULT Calculation(void *firstOp, int32_t firstType, void *secondOp, int32_t secondType, int32_t operationType,
                               int32_t &resultType, void **data, std::string &errorText);
    static HRESULT GenerateStackMachineProgram(const std::string &expr, void **ppStackProgram, std::string &textOutput);
    static void ReleaseStackMachineProgram(void *pStackProgram);
    static HRESULT NextStackCommand(void *pStackProgram, int32_t &Command, void **Ptr, std::string &textOutput);
    static void *AllocString(const std::string &str);
    static HRESULT StringToUpper(std::string &String);
    static BSTR SysAllocStringLen(int32_t size);
    static void SysFreeString(BSTR ptrBSTR);
    static void CoTaskMemFree(void *ptr);

  private:

    static RWLock CLRrwlock;
    static void *hostHandle;
    static unsigned int domainId;
    static coreclr_shutdown_ptr shutdownCoreClr;

    enum class RetCode : int32_t // NOLINT(performance-enum-size)
    {
        OK = 0,
        Fail = 1,
        Exception = 2
    };

    // CoreCLR uses fixed size integers, don't use system/arch size dependent types for delegates.
    // Important! In case of usage pointer to variable as delegate arg, make sure it has proper size for CoreCLR!
    // For example, native code "int" != managed code "int", since managed code "int" is 4 byte fixed size.
    using ReadMemoryDelegate = int (*)(const void *, char *, int32_t);
    using LoadSymbolsForModuleDelegate = void *(*)(const WCHAR *, BOOL, const void *, int32_t, const void *, int32_t, ReadMemoryDelegate, BSTR *);
    using DisposeDelegate = void (*)(void *);
    using GetLocalVariableNameAndScopeDelegate = RetCode (*)(void *, int32_t, uint32_t, BSTR *, int32_t *, int32_t *);
    using GetHoistedLocalScopesDelegate = RetCode (*)(void *, int32_t, void **, int32_t *);
    using GetSequencePointByILOffsetDelegate = RetCode (*)(void *, mdMethodDef, uint32_t, void *);
    using GetNextUserCodeILOffsetDelegate = RetCode (*)(void *, mdMethodDef, uint32_t, uint32_t *, int32_t *);
    using GetStepRangesFromIPDelegate = RetCode (*)(void *, uint32_t, mdMethodDef, uint32_t *, uint32_t *);
    using GetModuleMethodsRangesDelegate = RetCode (*)(void *, uint32_t, void *, uint32_t, void *, void **);
    using ResolveBreakPointsDelegate = RetCode (*)(void *, int32_t, void *, int32_t, int32_t, int32_t *, const WCHAR *, void **);
    using GetAsyncMethodSteppingInfoDelegate = RetCode (*)(void *, mdMethodDef, void **, int32_t *, uint32_t *);
    using GetLocalConstantsDelegate = RetCode (*)(void *, int32_t, uint32_t, void **, int32_t *);
    using CalculationDelegate = RetCode (*)(void *, int32_t, void *, int32_t, int32_t, int32_t *, void **, BSTR *);
    using GenerateStackMachineProgramDelegate = int (*)(const WCHAR *, void **, BSTR *);
    using ReleaseStackMachineProgramDelegate = void (*)(void *);
    using NextStackCommandDelegate = int (*)(void *, int32_t *, void **, BSTR *);
    using StringToUpperDelegate = RetCode (*)(const WCHAR *, BSTR *);
    using CoTaskMemFreeDelegate = void (*)(void *);
    using SysAllocStringLenDelegate = void *(*)(int32_t);
    using SysFreeStringDelegate = void (*)(void *);

    static LoadSymbolsForModuleDelegate loadSymbolsForModuleDelegate;
    static DisposeDelegate disposeDelegate;
    static GetLocalVariableNameAndScopeDelegate getLocalVariableNameAndScopeDelegate;
    static GetHoistedLocalScopesDelegate getHoistedLocalScopesDelegate;
    static GetSequencePointByILOffsetDelegate getSequencePointByILOffsetDelegate;
    static GetNextUserCodeILOffsetDelegate getNextUserCodeILOffsetDelegate;
    static GetStepRangesFromIPDelegate getStepRangesFromIPDelegate;
    static GetModuleMethodsRangesDelegate getModuleMethodsRangesDelegate;
    static ResolveBreakPointsDelegate resolveBreakPointsDelegate;
    static GetAsyncMethodSteppingInfoDelegate getAsyncMethodSteppingInfoDelegate;
    static GetLocalConstantsDelegate getLocalConstantsDelegate;
    static GenerateStackMachineProgramDelegate generateStackMachineProgramDelegate;
    static ReleaseStackMachineProgramDelegate releaseStackMachineProgramDelegate;
    static NextStackCommandDelegate nextStackCommandDelegate;
    static StringToUpperDelegate stringToUpperDelegate;
    static CoTaskMemFreeDelegate coTaskMemFreeDelegate;
    static SysAllocStringLenDelegate sysAllocStringLenDelegate;
    static SysFreeStringDelegate sysFreeStringDelegate;
    static CalculationDelegate calculationDelegate;

};

} // namespace dncdbg

#endif // MANAGED_INTEROP_H
