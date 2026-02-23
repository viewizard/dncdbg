// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoint_entry.h"
#include "debugger/breakpoints/breakpointutils.h"
#include "debuginfo/debuginfo.h"
#include "utils/utf.h"
#include <array>
#include <string>

#ifdef _MSC_VER
#include <palclr.h>
#endif

namespace dncdbg
{

namespace
{

mdMethodDef GetEntryPointTokenFromFile(const std::string &path)
{
    FILE *pFile = nullptr;

#ifdef _WIN32
    if (_wfopen_s(&pFile, to_utf16(path).c_str(), L"rb") != 0)
        return mdMethodDefNil;
#else
    pFile = fopen(path.c_str(), "rb"); // NOLINT(cppcoreguidelines-owning-memory)
#endif // _WIN32

    if (pFile == nullptr)
    {
        return mdMethodDefNil;
    }

    auto getEntryPointToken = [&]() -> mdMethodDef
    {
        IMAGE_DOS_HEADER dosHeader;
        IMAGE_NT_HEADERS32 ntHeaders;

        if ((fread(&dosHeader, sizeof(dosHeader), 1, pFile) != 1) ||
            (fseek(pFile, VAL32(dosHeader.e_lfanew), SEEK_SET) != 0) ||
            (fread(&ntHeaders, sizeof(ntHeaders), 1, pFile) != 1))
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
            if ((fseek(pFile, VAL32(dosHeader.e_lfanew), SEEK_SET) != 0) ||
                (fread(&ntHeaders64, sizeof(ntHeaders64), 1, pFile) != 1))
            {
                return mdMethodDefNil;
            }
            corRVA = VAL32(ntHeaders64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COMHEADER].VirtualAddress);
        }

        static constexpr LONG lLONG_MAX = 2147483647;
        LONG pos = VAL32(dosHeader.e_lfanew);
        if (pos < 0 || size_t(lLONG_MAX - pos) < sizeof(ntHeaders.Signature) + sizeof(ntHeaders.FileHeader) + VAL16(ntHeaders.FileHeader.SizeOfOptionalHeader))
        {
            return mdMethodDefNil;
        }
        pos += static_cast<LONG>(sizeof(ntHeaders.Signature) + sizeof(ntHeaders.FileHeader) + VAL16(ntHeaders.FileHeader.SizeOfOptionalHeader));

        if (fseek(pFile, pos, SEEK_SET) != 0)
        {
            return mdMethodDefNil;
        }

        for (int i = 0; i < VAL16(ntHeaders.FileHeader.NumberOfSections); i++)
        {
            IMAGE_SECTION_HEADER sectionHeader;

            if (fread(&sectionHeader, sizeof(sectionHeader), 1, pFile) != 1)
            {
                return mdMethodDefNil;
            }

            if (corRVA >= VAL32(sectionHeader.VirtualAddress) &&
                corRVA < VAL32(sectionHeader.VirtualAddress) + VAL32(sectionHeader.SizeOfRawData))
            {
                IMAGE_COR20_HEADER corHeader;
                const ULONG offset = (corRVA - VAL32(sectionHeader.VirtualAddress)) + VAL32(sectionHeader.PointerToRawData);
                if ((offset > static_cast<ULONG>(lLONG_MAX)) ||
                    (fseek(pFile, offset, SEEK_SET) != 0) ||
                    (fread(&corHeader, sizeof(corHeader), 1, pFile) != 1) ||

                    (VAL32(corHeader.Flags) & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT))
                {
                    return mdMethodDefNil;
                }

                return VAL32(corHeader.EntryPointToken); // NOLINT(cppcoreguidelines-pro-type-union-access)
            }
        }
        return mdMethodDefNil;
    };

    const mdMethodDef res = getEntryPointToken();
    static_cast<void>(fclose(pFile)); // NOLINT(cppcoreguidelines-owning-memory)

    return res;
}

// Try to setup proper entry breakpoint method token and IL offset for async Main method.
// [in] pModule - module with async Main method;
// [in] pMDImport - metadataimport interface for pModule;
// [in] pDebugInfo - all loaded modules debug related data;
// [in] mdMainClass - class token with Main method in module pModule;
// [out] entryPointToken - corrected method token;
// [out] entryPointOffset - corrected IL offset on first user code line.
HRESULT TrySetupAsyncEntryBreakpoint(ICorDebugModule *pModule, IMetaDataImport *pMDImport, DebugInfo *pDebugInfo,
                                     mdTypeDef mdMainClass, mdMethodDef &entryPointToken, uint32_t &entryPointOffset)
{
    // In case of async method, compiler use `Namespace.ClassName.<Main>()` as entry method, that call
    // `Namespace.ClassName.Main()`, that create `Namespace.ClassName.<Main>d__0` and start state machine routine.
    // In this case, "real entry method" with user code from initial `Main()` method will be in:
    // Namespace.ClassName.<Main>d__0.MoveNext()
    // Note, number in "<Main>d__0" class name could be different.
    // Note, `Namespace.ClassName` could be different (see `-main` compiler option).
    // Note, `Namespace.ClassName.<Main>d__0` type have enclosing class as method `Namespace.ClassName.<Main>()` class.
    HRESULT Status = S_OK;
    ULONG numTypedefs = 0;
    HCORENUM hEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    mdMethodDef resultToken = mdMethodDefNil;
    while(SUCCEEDED(pMDImport->EnumTypeDefs(&hEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0 && resultToken == mdMethodDefNil)
    {
        mdTypeDef mdEnclosingClass = mdTypeDefNil;
        if (FAILED(pMDImport->GetNestedClassProps(typeDef, &mdEnclosingClass) || mdEnclosingClass != mdMainClass))
        {
            continue;
        }

        DWORD flags = 0;
        std::array<WCHAR, mdNameLen> className{};
        ULONG classNameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, className.data(), mdNameLen, &classNameLen, &flags, nullptr));
        if (!starts_with(className.data(), W("<Main>d__")))
        {
            continue;
        }

        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(pMDImport->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            mdTypeDef memTypeDef = mdTypeDefNil;
            std::array<WCHAR, mdNameLen> funcName{};
            ULONG funcNameLen = 0;
            if (FAILED(pMDImport->GetMethodProps(methodDef, &memTypeDef, funcName.data(), mdNameLen, &funcNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (str_equal(funcName.data(), W("MoveNext")))
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

    // Note, in case of async `MoveNext` method, user code don't start from 0 IL offset.
    uint32_t ilNextOffset = 0;
    IfFailRet(pDebugInfo->GetNextUserCodeILOffsetInMethod(pModule, resultToken, 0, ilNextOffset));

    entryPointToken = resultToken;
    entryPointOffset = ilNextOffset;
    return S_OK;
}

} // unnamed namespace

HRESULT EntryBreakpoint::ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(m_entryMutex);

    if (!m_stopAtEntry || (m_iCorFuncBreakpoint != nullptr))
    {
        return S_FALSE;
    }

    HRESULT Status = S_OK;
    mdMethodDef entryPointToken = GetEntryPointTokenFromFile(GetModuleFileName(pModule));
    // Note, by some reason, in CoreCLR 6.0 System.Private.CoreLib.dll have Token "0" as entry point RVA.
    if (entryPointToken == mdMethodDefNil ||
        TypeFromToken(entryPointToken) != mdtMethodDef)
    {
        return S_FALSE;
    }

    uint32_t entryPointOffset = 0;
    ToRelease<IUnknown> trUnknown;
    ToRelease<IMetaDataImport> trMDImport;
    mdTypeDef mdMainClass = mdTypeDefNil;
    std::array<WCHAR, mdNameLen> funcName{};
    ULONG funcNameLen = 0;
    // If we can't setup entry point correctly for async method, leave it "as is".
    if (SUCCEEDED(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown)) &&
        SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport))) &&
        SUCCEEDED(trMDImport->GetMethodProps(entryPointToken, &mdMainClass, funcName.data(), mdNameLen, &funcNameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)) &&
        // The `Main` method is the entry point of a C# application. (Libraries and services do not require a Main method as an entry point.)
        // https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/main-and-command-args/
        // In case of async method as entry method, GetEntryPointTokenFromFile() should return compiler's generated method `<Main>`, plus,
        // this should be method without user code.
        str_equal(funcName.data(), W("<Main>")))
    {
        TrySetupAsyncEntryBreakpoint(pModule, trMDImport, m_sharedDebugInfo.get(), mdMainClass, entryPointToken, entryPointOffset);
    }

    ToRelease<ICorDebugFunction> pFunction;
    IfFailRet(pModule->GetFunctionFromToken(entryPointToken, &pFunction));
    ToRelease<ICorDebugCode> pCode;
    IfFailRet(pFunction->GetILCode(&pCode));
    ToRelease<ICorDebugFunctionBreakpoint> iCorFuncBreakpoint;
    IfFailRet(pCode->CreateBreakpoint(entryPointOffset, &iCorFuncBreakpoint));

    m_iCorFuncBreakpoint = iCorFuncBreakpoint.Detach();

    return S_OK;
}

HRESULT EntryBreakpoint::CheckBreakpointHit(ICorDebugBreakpoint *pBreakpoint)
{
    const std::scoped_lock<std::mutex> lock(m_entryMutex);

    if (!m_stopAtEntry || (m_iCorFuncBreakpoint == nullptr))
    {
        return S_FALSE; // S_FALSE - no error, but not affect on callback
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> pFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&pFunctionBreakpoint)));
    IfFailRet(BreakpointUtils::IsSameFunctionBreakpoint(pFunctionBreakpoint, m_iCorFuncBreakpoint));
    if (Status == S_FALSE)
    {
        return S_FALSE;
    }

    m_iCorFuncBreakpoint->Activate(FALSE);
    m_iCorFuncBreakpoint.Free();
    return S_OK;
}

void EntryBreakpoint::Delete()
{
    const std::scoped_lock<std::mutex> lock(m_entryMutex);

    if (m_iCorFuncBreakpoint == nullptr)
    {
        return;
    }

    m_iCorFuncBreakpoint->Activate(FALSE);
    m_iCorFuncBreakpoint.Free();
}

} // namespace dncdbg
