// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifdef _WIN32
#pragma once

#include <cassert>
#include <tuple>
#include <memory>
#include <utility>
#include <windows.h> // TODO
#include <winsock2.h>

namespace dncdbg
{

template <> struct IOSystemTraits<Win32PlatformTag>
{
    struct FileHandle
    {
        FileHandle() : handle(INVALID_HANDLE_VALUE), type(FileOrPipe)
        {
        }

        explicit operator bool() const
        {
            return handle != INVALID_HANDLE_VALUE;
        }

        enum FileType
        {
            FileOrPipe,
            Socket
        };

        FileHandle(HANDLE filefd) : handle(filefd), type(FileOrPipe)
        {
        }
        FileHandle(SOCKET sockfd) : handle((HANDLE)sockfd), type(Socket)
        {
        }

        HANDLE handle;
        enum FileType type;
    };

    struct AsyncHandle
    {
        HANDLE handle;
        std::unique_ptr<OVERLAPPED> overlapped;
        bool check_eof;

        // workaround: non-blocking reading from console
        void *buf;
        size_t count;

        AsyncHandle()
            : handle(INVALID_HANDLE_VALUE),
              overlapped(),
              check_eof(false),
              buf(nullptr),
              count(0)
        {
        }

        AsyncHandle(AsyncHandle &&other) noexcept
            : handle(other.handle),
              overlapped(std::move(other.overlapped)),
              check_eof(other.check_eof),
              buf(other.buf),
              count(other.count)
        {
            other.handle = INVALID_HANDLE_VALUE;
        }

        AsyncHandle &operator=(AsyncHandle &&other) noexcept
        {
            return this->~AsyncHandle(), *new (this) AsyncHandle(std::move(other));
        }

        explicit operator bool() const
        {
            return handle != INVALID_HANDLE_VALUE;
        }
    };

    using IOSystem = typename IOSystemImpl<IOSystemTraits<Win32PlatformTag>>;
    using IOResult = IOSystem::IOResult;

    static std::pair<FileHandle, FileHandle> unnamed_pipe();
    static FileHandle listen_socket(unsigned tcp_port);
    static IOResult set_inherit(const FileHandle &, bool);
    static IOResult read(const FileHandle &, void *buf, size_t count);
    static IOResult write(const FileHandle &, const void *buf, size_t count);
    static AsyncHandle async_read(const FileHandle &, void *buf, size_t count);
    static AsyncHandle async_write(const FileHandle &, const void *buf, size_t count);
    static bool async_wait(const IOSystem::AsyncHandleIterator &begin,
                           const IOSystem::AsyncHandleIterator &end, std::chrono::milliseconds);
    static IOResult async_cancel(AsyncHandle &);
    static IOResult async_result(AsyncHandle &);
    static IOResult close(const FileHandle &);

    static IOSystem::StdFiles get_std_files();

    struct StdIOSwap
    {
        using StdFiles = IOSystem::StdFiles;
        using StdFileType = IOSystem::StdFileType;
        StdIOSwap(const StdFiles &);
        ~StdIOSwap();

        StdIOSwap(StdIOSwap &&other)
        {
            m_valid = other.m_valid;
            if (!m_valid)
                return;

            other.m_valid = false;
            for (unsigned n = 0; n < std::tuple_size_v<StdFiles>; n++)
            {
                m_orig_handle[n] = other.m_orig_handle[n];
                m_orig_fd[n] = other.m_orig_fd[n];
            }
        }

        bool m_valid;
        HANDLE m_orig_handle[std::tuple_size_v<StdFiles>];
        int m_orig_fd[std::tuple_size_v<StdFiles>];
    };
};

} // namespace dncdbg
#endif // _WIN32
