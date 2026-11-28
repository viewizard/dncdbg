// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_MEMORYBUFFER_H
#define UTILS_MEMORYBUFFER_H

#include <cstddef>
#include <string>

namespace dncdbg
{

// Memory-mapped file buffer class for read-only access to file contents.
// Provides a platform-independent interface for memory-mapping files.
//
// The class does not keep a file descriptor or file handle open: it is closed as
// soon as the mapping is established, because the mapping keeps the file alive on
// its own. While the buffer is open, only the mapping is held. On Windows, the
// mapping handle is kept as well, so that Flush() can recreate the view.
class MemoryBuffer
{
  public:

    MemoryBuffer() = default;
    ~MemoryBuffer();

    MemoryBuffer(const MemoryBuffer &) = delete;
    MemoryBuffer &operator=(const MemoryBuffer &) = delete;

    MemoryBuffer(MemoryBuffer &&other) noexcept;
    MemoryBuffer &operator=(MemoryBuffer &&other) noexcept;

    // Opens and memory-maps the specified file for read-only access.
    // Returns true on success, false on failure. A previously mapped file is
    // released first, so the object can be reused for another file.
    [[nodiscard]] bool Open(const std::string &filePath);

    // Releases the mapped memory back to the operating system by dropping the
    // mapped pages, while keeping the buffer open. Data is re-read from the
    // file on the next access.
    // Note: on Windows the mapped view is recreated, so Data() may return a
    // different pointer; always re-query Data() after calling this method.
    void Flush();

    // Returns a pointer to the mapped file data, or nullptr if not opened.
    [[nodiscard]] const void *Data() const noexcept
    {
        return m_mappedData;
    }

    // Returns the size of the mapped file in bytes, or 0 if not opened.
    [[nodiscard]] size_t Size() const noexcept
    {
        return m_fileSize;
    }

  private:

    // Unmaps the view and closes the mapping handle, if any. Safe to call on a closed buffer.
    void Release() noexcept;

    // Size of the mapped file in bytes.
    size_t m_fileSize = 0;
    void *m_mappedData = nullptr;

#ifdef _WIN32
    // File mapping handle, needed by Flush() to recreate the mapped view.
    // The file handle itself is closed right after the mapping is created.
    void *m_mappingHandle = nullptr;
#endif
};

} // namespace dncdbg

#endif // UTILS_MEMORYBUFFER_H
