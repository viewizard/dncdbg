// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_DBGSHIM_H
#define UTILS_DBGSHIM_H

#include <cor.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#include <pal_mstypes.h>
#else
#include <palclr.h>
#include <wtypes.h>
#endif

#include "utils/dynlibs.h"
#include "utils/filesystem.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dncdbg
{

// Based on coreclr/src/dlls/dbgshim/dbgshim.h
class DbgShim
{
  public:

    using PSTARTUP_CALLBACK = void (*)(IUnknown *, void *, HRESULT);
    using CreateProcessForLaunch_t = HRESULT (*)(WCHAR *, BOOL, void *, const WCHAR *, DWORD *, HANDLE *);
    using ResumeProcess_t = HRESULT (*)(HANDLE);
    using CloseResumeHandle_t = HRESULT (*)(HANDLE);
    using RegisterForRuntimeStartup_t = HRESULT (*)(DWORD, PSTARTUP_CALLBACK, void *, void **);
    using UnregisterForRuntimeStartup_t = HRESULT (*)(void *);

    DbgShim()
    {
        // The dbgshim library is located next to the debugger executable, so the executable path
        // is reused as the buffer for the library path to avoid extra string copies.
        std::string libPath = GetExeAbsPath();
        if (libPath.empty())
        {
            throw std::runtime_error("Unable to detect exe path");
        }

        const std::size_t dirSepIndex = libPath.find_last_of(FileSystem::PathSeparatorSymbols);
        if (dirSepIndex == std::string::npos)
        {
            throw std::runtime_error("Unable to detect exe directory: " + libPath);
        }

#if (defined(__APPLE__) && defined(__MACH__))
        constexpr std::string_view libFileName("libdbgshim.dylib");
#elif defined(__linux__)
        constexpr std::string_view libFileName("libdbgshim.so");
#elif defined(_WIN32)
        constexpr std::string_view libFileName("dbgshim.dll");
#else
#error "Unsupported platform"
#endif

        libPath.resize(dirSepIndex + 1);
        libPath.reserve(libPath.size() + libFileName.size()); // Avoid reallocation while appending the file name.
        libPath += libFileName;

        m_module = DLOpen(libPath.c_str());
        if (m_module == nullptr)
        {
            throw std::invalid_argument("Unable to load " + libPath);
        }

        m_createProcessForLaunch = reinterpret_cast<CreateProcessForLaunch_t>(DLSym(m_module, "CreateProcessForLaunch"));
        m_resumeProcess = reinterpret_cast<ResumeProcess_t>(DLSym(m_module, "ResumeProcess"));
        m_closeResumeHandle = reinterpret_cast<CloseResumeHandle_t>(DLSym(m_module, "CloseResumeHandle"));
        m_registerForRuntimeStartup = reinterpret_cast<RegisterForRuntimeStartup_t>(DLSym(m_module, "RegisterForRuntimeStartup"));
        m_unregisterForRuntimeStartup = reinterpret_cast<UnregisterForRuntimeStartup_t>(DLSym(m_module, "UnregisterForRuntimeStartup"));

        const bool dlsymSuccess = m_createProcessForLaunch != nullptr &&
                                  m_resumeProcess != nullptr &&
                                  m_closeResumeHandle != nullptr &&
                                  m_registerForRuntimeStartup != nullptr &&
                                  m_unregisterForRuntimeStartup != nullptr;

        if (!dlsymSuccess)
        {
            // The destructor is not called if the constructor throws,
            // so release the library handle here to avoid a leak.
            DLClose(m_module);
            throw std::invalid_argument("Unable to dlsym for dbgshim module");
        }
    }

    DbgShim(DbgShim &&) = delete;
    DbgShim(const DbgShim &) = delete;
    DbgShim &operator=(DbgShim &&) = delete;
    DbgShim &operator=(const DbgShim &) = delete;

    [[nodiscard]] CreateProcessForLaunch_t GetCreateProcessForLaunch() const noexcept
    {
        return m_createProcessForLaunch;
    }

    [[nodiscard]] ResumeProcess_t GetResumeProcess() const noexcept
    {
        return m_resumeProcess;
    }

    [[nodiscard]] CloseResumeHandle_t GetCloseResumeHandle() const noexcept
    {
        return m_closeResumeHandle;
    }

    [[nodiscard]] RegisterForRuntimeStartup_t GetRegisterForRuntimeStartup() const noexcept
    {
        return m_registerForRuntimeStartup;
    }

    [[nodiscard]] UnregisterForRuntimeStartup_t GetUnregisterForRuntimeStartup() const noexcept
    {
        return m_unregisterForRuntimeStartup;
    }

    ~DbgShim()
    {
        if (m_module != nullptr)
        {
            DLClose(m_module);
        }
    }

  private:

    CreateProcessForLaunch_t m_createProcessForLaunch{nullptr};
    ResumeProcess_t m_resumeProcess{nullptr};
    CloseResumeHandle_t m_closeResumeHandle{nullptr};
    RegisterForRuntimeStartup_t m_registerForRuntimeStartup{nullptr};
    UnregisterForRuntimeStartup_t m_unregisterForRuntimeStartup{nullptr};

    DLHandle m_module{nullptr};
};

} // namespace dncdbg

#endif // UTILS_DBGSHIM_H
