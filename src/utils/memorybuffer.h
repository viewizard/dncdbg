// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_MEMORYBUFFER_H
#define UTILS_MEMORYBUFFER_H

#ifdef _WIN32
#include <windows.h>
#endif
#include <string>

namespace dncdbg
{

// Memory-mapped file buffer class for read-only access to file contents.
// Provides a platform-independent interface for memory-mapping files.
class MemoryBuffer
{
  public:

    MemoryBuffer() = default;
    ~MemoryBuffer();

    MemoryBuffer(const MemoryBuffer &) = delete;
    MemoryBuffer &operator=(const MemoryBuffer &) = delete;

    MemoryBuffer(MemoryBuffer &&other) noexcept
        : m_mappedData(other.m_mappedData),
          m_fileSize(other.m_fileSize),
#ifdef _WIN32
          m_fileHandle(other.m_fileHandle),
          m_mappingHandle(other.m_mappingHandle)
#else
          m_fd(other.m_fd)
#endif
    {
        other.m_mappedData = nullptr;
        other.m_fileSize = 0;
#ifdef _WIN32
        other.m_fileHandle = INVALID_HANDLE_VALUE;
        other.m_mappingHandle = nullptr;
#else
        other.m_fd = -1;
#endif
    }

    MemoryBuffer &operator=(MemoryBuffer &&other) noexcept;

    // Opens and memory-maps the specified file for read-only access.
    // Returns true on success, false on failure.
    bool Open(const std::string &filePath);

    // Returns a pointer to the mapped file data, or nullptr if not opened.
    [[nodiscard]] const void *Data() const
    {
        return m_mappedData;
    }

    // Returns the size of the mapped file in bytes, or 0 if not opened.
    [[nodiscard]] size_t Size() const
    {
        return m_fileSize;
    }

  private:

    void *m_mappedData = nullptr;
    size_t m_fileSize = 0;

#ifdef _WIN32
    HANDLE m_fileHandle = INVALID_HANDLE_VALUE;
    HANDLE m_mappingHandle = nullptr;
#else
    int m_fd = -1;
#endif
};

} // namespace dncdbg

#endif // UTILS_MEMORYBUFFER_H
