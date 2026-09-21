// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoint_entry.h"
#include "debugger/breakpoints/helpers.h"
#include "debuginfo/debuginfo.h"
#include "metadata/modules.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <fstream>
#include <mutex>
#include <string>

#ifdef _MSC_VER
#include <palclr.h>
#endif

namespace dncdbg::EntryBreakpoint
{

namespace
{

std::mutex &GetEntryMutex()
{
    static std::mutex entryMutex;
    return entryMutex;
}

ToRelease<ICorDebugFunctionBreakpoint> &GetFuncBreakpoint()
{
    static ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
    return trFuncBreakpoint;
}

bool &GetStopAtEntry()
{
    static bool stopAtEntry{false};
    return stopAtEntry;
}

mdMethodDef GetEntryPointTokenFromFile(const std::string &path)
{
#ifdef _WIN32
    std::ifstream file(to_utf16(path).c_str(), std::ios::binary);
#else
    std::ifstream file(path, std::ios::binary);
#endif // _WIN32
    if (!file.is_open())
    {
        return mdMethodDefNil;
    }

    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS32 ntHeaders;

    if (!file.read(reinterpret_cast<char *>(&dosHeader), sizeof(dosHeader)) ||
        !file.seekg(VAL32(dosHeader.e_lfanew), std::ios::beg) ||
        !file.read(reinterpret_cast<char *>(&ntHeaders), sizeof(ntHeaders)))
    {
        return mdMethodDefNil;
    }

    ULONG corRVA = 0;
    if (ntHeaders.OptionalHeader.Magic == VAL16(IMAGE_NT_OPTIONAL_HDR32_MAGIC))
    {
        corRVA = VAL32(ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COMHEADER].VirtualAddress);
    }
    else
    {
        IMAGE_NT_HEADERS64 ntHeaders64;
        if (!file.seekg(VAL32(dosHeader.e_lfanew), std::ios::beg) ||
            !file.read(reinterpret_cast<char *>(&ntHeaders64), sizeof(ntHeaders64)))
        {
            return mdMethodDefNil;
        }
        corRVA = VAL32(ntHeaders64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COMHEADER].VirtualAddress);
    }

    static constexpr LONG lLONG_MAX = 2147483647;
    LONG pos = VAL32(dosHeader.e_lfanew);
    if (pos < 0 || static_cast<size_t>(lLONG_MAX - pos) < sizeof(ntHeaders.Signature) + sizeof(ntHeaders.FileHeader) + VAL16(ntHeaders.FileHeader.SizeOfOptionalHeader))
    {
        return mdMethodDefNil;
    }
    pos += static_cast<LONG>(sizeof(ntHeaders.Signature) + sizeof(ntHeaders.FileHeader) + VAL16(ntHeaders.FileHeader.SizeOfOptionalHeader));

    if (!file.seekg(pos, std::ios::beg))
    {
        return mdMethodDefNil;
    }

    for (int i = 0; i < VAL16(ntHeaders.FileHeader.NumberOfSections); i++)
    {
        IMAGE_SECTION_HEADER sectionHeader;

        if (!file.read(reinterpret_cast<char *>(&sectionHeader), sizeof(sectionHeader)))
        {
            return mdMethodDefNil;
        }

        if (corRVA >= VAL32(sectionHeader.VirtualAddress) &&
            corRVA < VAL32(sectionHeader.VirtualAddress) + VAL32(sectionHeader.SizeOfRawData))
        {
            IMAGE_COR20_HEADER corHeader;
            const ULONG offset = (corRVA - VAL32(sectionHeader.VirtualAddress)) + VAL32(sectionHeader.PointerToRawData);
            if ((offset > static_cast<ULONG>(lLONG_MAX)) ||
                !file.seekg(offset, std::ios::beg) ||
                !file.read(reinterpret_cast<char *>(&corHeader), sizeof(corHeader)) ||

                (VAL32(corHeader.Flags) & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT) != 0U)
            {
                return mdMethodDefNil;
            }

            return VAL32(corHeader.EntryPointToken); // NOLINT(cppcoreguidelines-pro-type-union-access)
        }
    }

    return mdMethodDefNil;
}

// Try to set up proper entry breakpoint method token and IL offset for the async Main method.
// [in] pModule - module with async Main method;
// [in] pMDImport - metadata import interface for pModule;
// [in] mdMainClass - class token with Main method in module pModule;
// [out] entryPointToken - corrected method token;
// [out] entryPointOffset - corrected IL offset on first user code line.
HRESULT TrySetupAsyncEntryBreakpoint(ICorDebugModule *pModule, IMetaDataImport *pMDImport,
                                     mdTypeDef mdMainClass, mdMethodDef &entryPointToken, uint32_t &entryPointOffset)
{
    // In case of an async method, the compiler uses `Namespace.ClassName.<Main>()` as the entry method, which calls
    // `Namespace.ClassName.Main()`, which creates `Namespace.ClassName.<Main>d__0` and starts the state machine routine.
    // In this case, the "real entry method" with user code from the initial `Main()` method will be in:
    // Namespace.ClassName.<Main>d__0.MoveNext()
    // Note, the number in the "<Main>d__0" class name could be different.
    // Note, `Namespace.ClassName` could be different (see the `-main` compiler option).
    // Note, the `Namespace.ClassName.<Main>d__0` type has the same enclosing class as the `Namespace.ClassName.<Main>()` method.
    HRESULT Status = S_OK;
    ULONG numTypedefs = 0;
    HCORENUM hEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    mdMethodDef resultToken = mdMethodDefNil;
    while (SUCCEEDED(pMDImport->EnumTypeDefs(&hEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0 && resultToken == mdMethodDefNil)
    {
        mdTypeDef mdEnclosingClass = mdTypeDefNil;
        if (FAILED(pMDImport->GetNestedClassProps(typeDef, &mdEnclosingClass) || mdEnclosingClass != mdMainClass))
        {
            continue;
        }

        ULONG classNameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, nullptr, 0, &classNameLen, nullptr, nullptr));

        DWORD flags = 0;
        WSTRING className(classNameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, className.data(), classNameLen, nullptr, &flags, nullptr));
        // Remove null terminator that was included in the length
        if (!className.empty() && className.back() == '\0')
        {
            className.pop_back();
        }
        if (className.find(W("<Main>d__")) != 0)
        {
            continue;
        }

        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(pMDImport->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            ULONG funcNameLen = 0;
            if (FAILED(pMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &funcNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            WSTRING funcName(funcNameLen, '\0');
            if (FAILED(pMDImport->GetMethodProps(methodDef, nullptr, funcName.data(), funcNameLen, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Remove null terminator that was included in the length
            if (!funcName.empty() && funcName.back() == '\0')
            {
                funcName.pop_back();
            }

            if (funcName == W("MoveNext"))
            {
                resultToken = methodDef;
                break;
            }
        }
        pMDImport->CloseEnum(fEnum);
    }
    pMDImport->CloseEnum(hEnum);

    if (resultToken == mdMethodDefNil)
    {
        return E_FAIL;
    }

    // Note, in case of an async `MoveNext` method, user code does not start from 0 IL offset.
    uint32_t ilNextOffset = 0;
    IfFailRet(DebugInfo::GetNextUserCodeILOffset(pModule, resultToken, 0, ilNextOffset));

    entryPointToken = resultToken;
    entryPointOffset = ilNextOffset;
    return S_OK;
}

} // unnamed namespace

void SetStopAtEntry(bool enable)
{
    const std::scoped_lock<std::mutex> lock(GetEntryMutex());
    GetStopAtEntry() = enable;
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetEntryMutex());

    if (!GetStopAtEntry() || (GetFuncBreakpoint() != nullptr))
    {
        return S_FALSE;
    }

    HRESULT Status = S_OK;
    mdMethodDef entryPointToken = GetEntryPointTokenFromFile(Modules::GetModuleFilePath(pModule));
    // Note, for some reason, in CoreCLR 6.0 System.Private.CoreLib.dll has token "0" as the entry point RVA.
    if (entryPointToken == mdMethodDefNil ||
        TypeFromToken(entryPointToken) != mdtMethodDef)
    {
        return S_FALSE;
    }

    uint32_t entryPointOffset = 0;
    const auto setupAsyncEntryBreakpoint = [&]() -> HRESULT
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
        ULONG funcNameLen = 0;
        IfFailRet(trMDImport->GetMethodProps(entryPointToken, nullptr, nullptr, 0, &funcNameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef mdMainClass = mdTypeDefNil;
        WSTRING funcName(funcNameLen, '\0');
        IfFailRet(trMDImport->GetMethodProps(entryPointToken, &mdMainClass, funcName.data(), funcNameLen, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr));
        // Remove null terminator that was included in the length
        if (!funcName.empty() && funcName.back() == '\0')
        {
            funcName.pop_back();
        }
        // The `Main` method is the entry point of a C# application. (Libraries and services do not require a Main method as an entry point.)
        // https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/main-and-command-args/
        // In case of an async method as the entry method, GetEntryPointTokenFromFile() should return the compiler-generated method `<Main>`, plus,
        // this should be a method without user code.
        if (funcName == W("<Main>"))
        {
            TrySetupAsyncEntryBreakpoint(pModule, trMDImport, mdMainClass, entryPointToken, entryPointOffset);
        }
        return S_OK;
    };
    // If we can't set up the entry point correctly for an async method, leave it "as is".
    setupAsyncEntryBreakpoint();

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));
    ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
    IfFailRet(BreakpointHelpers::ActivateManagedBreakpoint(modAddress, entryPointToken, entryPointOffset, pModule, &trFuncBreakpoint));
    GetFuncBreakpoint() = trFuncBreakpoint.Detach();

    return S_OK;
}

HRESULT CheckBreakpointHit(ICorDebugBreakpoint *pBreakpoint)
{
    const std::scoped_lock<std::mutex> lock(GetEntryMutex());

    if (!GetStopAtEntry() ||
        GetFuncBreakpoint() == nullptr)
    {
        return S_FALSE;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> trFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&trFunctionBreakpoint)));
    IfFailRet(BreakpointHelpers::IsSameFunctionBreakpoint(trFunctionBreakpoint, GetFuncBreakpoint()));
    if (Status == S_FALSE)
    {
        return S_FALSE;
    }

    BreakpointHelpers::DeactivateManagedBreakpoint(GetFuncBreakpoint());
    return S_OK;
}

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetEntryMutex());
    GetFuncBreakpoint().Free();
}

} // namespace dncdbg::EntryBreakpoint
