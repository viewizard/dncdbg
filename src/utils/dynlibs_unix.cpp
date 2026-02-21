// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

#include "utils/dynlibs.h"
#include "utils/limits.h" // NOLINT(misc-include-cleaner)
#include "utils/logger.h"
#include <dlfcn.h>
#include <array>
#include <iostream>

namespace dncdbg
{

// This functon load specified library and returns handle (which then
// can be passed to DLSym and DLCLose functions).
// In case of error function returns nullptr.
DLHandle DLOpen(const std::string &path)
{
    void *tmpPointer = ::dlopen(path.c_str(), RTLD_GLOBAL | RTLD_NOW);
    if (tmpPointer == nullptr)
    {
        const char *err = ::dlerror();
        std::cerr << "dlopen() error: " << err << "\n";
        LOGE("dlopen() error: %s", err);
    }
    return reinterpret_cast<DLHandle>(tmpPointer);
}

// This function resolves symbol address within library specified by handle,
// and returns it's address, in case of error function returns nullptr.
void *DLSym(DLHandle handle, const std::string_view &symbol)
{
    std::array<char, LINE_MAX> str{};
    if (symbol.size() >= str.size())
    {
        return {};
    }

    symbol.copy(str.data(), symbol.size());
    str[symbol.size()] = 0;

    ::dlerror(); // Clear any existing error

    void *tmpPointer = ::dlsym(handle, str.data()); // NOLINT(misc-const-correctness)

    const char *err = ::dlerror();
    if (err != nullptr)
    {
        std::cerr << "dlsym() error: " << err << "\n";
        LOGE("dlsym() error: %s", err);
    }

    return tmpPointer;
}

// This function unloads previously loadded library, specified by handle.
// In case of error this function returns `false'.
bool DLClose(DLHandle handle)
{
    const int ret = ::dlclose(handle);
    if (ret != 0)
    {
        const char *err = ::dlerror();
        std::cerr << "dlclose() error: " << err << "\n";
        LOGE("dlclose() error: %s", err);
    }

    return (ret != 0);
}

} // namespace dncdbg
#endif // __unix__
