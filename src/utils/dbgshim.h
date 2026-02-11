// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <palclr.h>
#include <wtypes.h>
#endif

#include "utils/dynlibs.h"
#include "utils/filesystem.h"
#include <string>
#include <stdexcept>

namespace dncdbg
{

// Based on coreclr/src/dlls/dbgshim/dbgshim.h
struct dbgshim_t
{
    using PSTARTUP_CALLBACK = void (*)(IUnknown *, void *, HRESULT);
    using CreateProcessForLaunch_t = HRESULT (*)(WCHAR *, BOOL, void *, const WCHAR *, DWORD *, HANDLE *);
    using ResumeProcess_t = HRESULT (*)(HANDLE);
    using CloseResumeHandle_t = HRESULT (*)(HANDLE);
    using RegisterForRuntimeStartup_t = HRESULT (*)(DWORD, PSTARTUP_CALLBACK, void *, void **);
    using UnregisterForRuntimeStartup_t = HRESULT (*)(void *);
    using EnumerateCLRs_t = HRESULT (*)(DWORD, HANDLE **, LPWSTR **, DWORD *);
    using CloseCLREnumeration_t = HRESULT (*)(HANDLE *, LPWSTR *, DWORD );
    using CreateVersionStringFromModule_t = HRESULT (*)(DWORD, const WCHAR *, WCHAR *, DWORD, DWORD *);
    using CreateDebuggingInterfaceFromVersionEx_t = HRESULT (*)(int, const WCHAR *, IUnknown **);
    CreateProcessForLaunch_t CreateProcessForLaunch;
    ResumeProcess_t ResumeProcess;
    CloseResumeHandle_t CloseResumeHandle;
    RegisterForRuntimeStartup_t RegisterForRuntimeStartup;
    UnregisterForRuntimeStartup_t UnregisterForRuntimeStartup;
    EnumerateCLRs_t EnumerateCLRs;
    CloseCLREnumeration_t CloseCLREnumeration;
    CreateVersionStringFromModule_t CreateVersionStringFromModule;
    CreateDebuggingInterfaceFromVersionEx_t CreateDebuggingInterfaceFromVersionEx;

    dbgshim_t()
        : CreateProcessForLaunch(nullptr),
          ResumeProcess(nullptr),
          CloseResumeHandle(nullptr),
          RegisterForRuntimeStartup(nullptr),
          UnregisterForRuntimeStartup(nullptr),
          EnumerateCLRs(nullptr),
          CloseCLREnumeration(nullptr),
          CreateVersionStringFromModule(nullptr),
          CreateDebuggingInterfaceFromVersionEx(nullptr),
          m_module(nullptr)
    {
        std::string exe = GetExeAbsPath();
        if (exe.empty())
            throw std::runtime_error("Unable to detect exe path");

        std::size_t dirSepIndex = exe.rfind(DIRECTORY_SEPARATOR_STR_A);
        if (dirSepIndex == std::string::npos)
            return;
        std::string libName = exe.substr(0, dirSepIndex + 1);

#ifdef _WIN32
        libName += "dbgshim.dll";
#elif defined(__APPLE__)
        libName += "libdbgshim.dylib";
#else
        libName += "libdbgshim.so";
#endif

        m_module = DLOpen(libName);
        if (!m_module)
            throw std::invalid_argument("Unable to load " + libName);

        CreateProcessForLaunch = reinterpret_cast<CreateProcessForLaunch_t>(DLSym(m_module, "CreateProcessForLaunch"));
        ResumeProcess = reinterpret_cast<ResumeProcess_t>(DLSym(m_module, "ResumeProcess"));
        CloseResumeHandle = reinterpret_cast<CloseResumeHandle_t>(DLSym(m_module, "CloseResumeHandle"));
        RegisterForRuntimeStartup = reinterpret_cast<RegisterForRuntimeStartup_t>(DLSym(m_module, "RegisterForRuntimeStartup"));
        UnregisterForRuntimeStartup = reinterpret_cast<UnregisterForRuntimeStartup_t>(DLSym(m_module, "UnregisterForRuntimeStartup"));
        EnumerateCLRs = reinterpret_cast<EnumerateCLRs_t>(DLSym(m_module, "EnumerateCLRs"));
        CloseCLREnumeration = reinterpret_cast<CloseCLREnumeration_t>(DLSym(m_module, "CloseCLREnumeration"));
        CreateVersionStringFromModule = reinterpret_cast<CreateVersionStringFromModule_t>(DLSym(m_module, "CreateVersionStringFromModule"));
        CreateDebuggingInterfaceFromVersionEx = reinterpret_cast<CreateDebuggingInterfaceFromVersionEx_t>(DLSym(m_module, "CreateDebuggingInterfaceFromVersionEx"));

        bool dlsym_ok = CreateProcessForLaunch &&
                        ResumeProcess &&
                        CloseResumeHandle &&
                        RegisterForRuntimeStartup &&
                        UnregisterForRuntimeStartup &&
                        EnumerateCLRs &&
                        CloseCLREnumeration &&
                        CreateVersionStringFromModule &&
                        CreateDebuggingInterfaceFromVersionEx;

        if (!dlsym_ok)
            throw std::invalid_argument("Unable to dlsym for dbgshim module");
    }

    ~dbgshim_t()
    {
        if (m_module)
            DLClose(m_module);
    }

  private:

    DLHandle m_module;
};

} // namespace dncdbg
