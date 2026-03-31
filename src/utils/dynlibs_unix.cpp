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
#include <iostream>

namespace dncdbg
{

// This function loads the specified library and returns a handle (which then
// can be passed to DLSym and DLClose functions).
// In case of error, the function returns nullptr.
DLHandle DLOpen(const char *path)
{
    void *tmpPointer = ::dlopen(path, RTLD_GLOBAL | RTLD_NOW);
    if (tmpPointer == nullptr)
    {
        const char *err = ::dlerror();
        std::cerr << "dlopen() error: " << err << "\n";
        LOGE(log << "dlopen() error: " << err);
    }
    return reinterpret_cast<DLHandle>(tmpPointer);
}

// This function resolves the symbol address within the library specified by handle,
// and returns its address; in case of error, the function returns nullptr.
void *DLSym(DLHandle handle, const char *symbol)
{
    ::dlerror(); // Clear any existing error

    void *tmpPointer = ::dlsym(handle, symbol);

    const char *err = ::dlerror();
    if (err != nullptr)
    {
        std::cerr << "dlsym() error: " << err << "\n";
        LOGE(log << "dlsym() error: " << err);
    }

    return tmpPointer;
}

// This function unloads the previously loaded library specified by handle.
// In case of error, this function returns `false`.
bool DLClose(DLHandle handle)
{
    const int ret = ::dlclose(handle);
    if (ret != 0)
    {
        const char *err = ::dlerror();
        std::cerr << "dlclose() error: " << err << "\n";
        LOGE(log << "dlclose() error: " << err);
    }

    return (ret != 0);
}

} // namespace dncdbg
#endif // __unix__
