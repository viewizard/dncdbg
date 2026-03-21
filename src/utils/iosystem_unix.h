// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_IOSYSTEM_UNIX_H
#define UTILS_IOSYSTEM_UNIX_H

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

#include "utils/iosystem_internal.h"
#include "utils/platform.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <new>
#include <sys/select.h>
#include <tuple>
#include <utility>

template <> struct dncdbg::IOSystemTraits<dncdbg::UnixPlatformTag>
{
    using IOSystem = dncdbg::IOSystemImpl<IOSystemTraits<UnixPlatformTag>>;
    using IOResult = IOSystem::IOResult;

    struct FileHandle
    {
        FileHandle() : fd(-1)
        {
        }
        FileHandle(int n) : fd(n)
        {
        }
        explicit operator bool() const
        {
            return fd != -1;
        }

        int fd;
    };

    struct AsyncHandle // NOLINT(cppcoreguidelines-pro-type-member-init)
    {
        struct Traits
        {
            IOResult (*oper)(void *thiz);
            int (*poll)(void *thiz, fd_set *, fd_set *, fd_set *);
            void (*move)(void *src, void *dst);
            void (*destr)(void *thiz);
        };

        template <typename T> struct TraitsImpl
        {
            static struct Traits traits;
        };

        const Traits *traits{nullptr};
        mutable char data alignas(__BIGGEST_ALIGNMENT__)[sizeof(void *) * 4]; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

        explicit operator bool() const
        {
            return (traits != nullptr);
        }

        IOResult operator()()
        {
            assert(*this);
            return traits->oper(data);
        }

        int poll(fd_set *read, fd_set *write, fd_set *except)
        {
            assert(*this);
            return traits->poll(data, read, write, except);
        }

        AsyncHandle() = default; // NOLINT(cppcoreguidelines-pro-type-member-init)

        template <typename InstanceType, typename... Args> static AsyncHandle create(Args &&...args)
        {
            static_assert(sizeof(InstanceType) <= sizeof(data), "insufficiend data size");
            AsyncHandle result;
            result.traits = &TraitsImpl<InstanceType>::traits;
            new (result.data) InstanceType(std::forward<Args>(args)...);
            return result;
        }

        AsyncHandle(AsyncHandle &&other) noexcept // NOLINT(cppcoreguidelines-pro-type-member-init)
            : traits(other.traits)
        {
            if (other)
            {
                traits->move(other.data, data);
            }
            other.traits = nullptr;
        }

        AsyncHandle &operator=(AsyncHandle &&other) noexcept
        {
            this->~AsyncHandle();
            return *new (this) AsyncHandle(std::move(other)); // NOLINT(cppcoreguidelines-c-copy-assignment-signature,misc-unconventional-assign-operator)
        }

        AsyncHandle(const AsyncHandle &) = delete;
        AsyncHandle &operator=(const AsyncHandle &) = delete;

        ~AsyncHandle()
        {
            if (*this)
            {
                traits->destr(data);
            }
        }
    };

    static std::pair<FileHandle, FileHandle> unnamed_pipe();
    static FileHandle listen_socket(unsigned tcp_port);
    static IOResult set_inherit(const FileHandle &fh, bool inherit);
    static IOResult read(const FileHandle &fh, void *buf, size_t count);
    static IOResult write(const FileHandle &fh, const void *buf, size_t count);
    static AsyncHandle async_read(const FileHandle &fh, void *buf, size_t count);
    static AsyncHandle async_write(const FileHandle &fh, const void *buf, size_t count);
    static bool async_wait(const IOSystem::AsyncHandleIterator &begin,
                           const IOSystem::AsyncHandleIterator &end, std::chrono::milliseconds timeout);
    static IOResult async_cancel(AsyncHandle &handle);
    static IOResult async_result(AsyncHandle &handle);
    static IOResult close(const FileHandle &fh);

    struct StdIOSwap
    {
        using StdFiles = IOSystem::StdFiles;
        using StdFileType = IOSystem::StdFileType;
        StdIOSwap(const StdFiles &files);
        ~StdIOSwap();

        StdIOSwap(StdIOSwap &&other) noexcept // NOLINT(cppcoreguidelines-pro-type-member-init)
            : m_valid(other.m_valid)
        {
            if (!m_valid)
            {
                return;
            }

            other.m_valid = false;
            for (unsigned n = 0; n < std::tuple_size_v<StdFiles>; n++)
            {
                m_orig_fd[n] = other.m_orig_fd[n];
            }
        }

        StdIOSwap(const StdIOSwap &) = delete;
        StdIOSwap &operator=(StdIOSwap &&) = delete;
        StdIOSwap &operator=(const StdIOSwap &) = delete;

        bool m_valid{true};
        std::array<int, std::tuple_size_v<StdFiles>> m_orig_fd;
    };

    static IOSystem::StdFiles get_std_files();
};

#endif // __unix__

#endif // UTILS_IOSYSTEM_UNIX_H
