// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

#include "utils/dynlibs.h"
#include "utils/limits.h" // NOLINT(misc-include-cleaner)
#include "utils/logger.h"
#include <dlfcn.h>
#include <array>

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
        static_cast<void>(fprintf(stderr, "dlopen() error: %s\n", err));
        LOGE("dlopen() error: %s", err);
    }
    return reinterpret_cast<DLHandle>(tmpPointer);
}

// This function resolves symbol address within library specified by handle,
// and returns it's address, in case of error function returns nullptr.
void *DLSym(DLHandle handle, const std::string_view &name)
{
    std::array<char, LINE_MAX> str{};
    if (name.size() >= str.size())
        return {};

    name.copy(str.data(), name.size());
    str[name.size()] = 0;

    ::dlerror(); // Clear any existing error

    void *tmpPointer = ::dlsym(handle, str.data()); // NOLINT(misc-const-correctness)

    const char *err = ::dlerror();
    if (err != nullptr)
    {
        static_cast<void>(fprintf(stderr, "dlsym() error: %s\n", err));
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
        static_cast<void>(fprintf(stderr, "dlclose() error: %s\n", err));
        LOGE("dlclose() error: %s", err);
    }

    return ret;
}

} // namespace dncdbg
#endif // __unix__
