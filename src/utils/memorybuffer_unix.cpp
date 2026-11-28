// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/memorybuffer.h"
#include "utils/logger.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace dncdbg
{

namespace
{

// Limit the file size to 4 GB for consistency across platforms, since the
// metadata reader takes the size as a 32-bit value.
constexpr uint64_t maxFileSize = std::numeric_limits<uint32_t>::max();

} // unnamed namespace

bool MemoryBuffer::Open(const std::string &filePath)
{
    // Release a previously mapped file, so the object can be reused.
    Release();

    const int fd = open(filePath.c_str(), O_RDONLY | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg,bugprone-signed-bitwise)
    if (fd == -1)
    {
        return false;
    }

    struct stat sb{};
    if (fstat(fd, &sb) == -1)
    {
        const int statError = errno;
        static_cast<void>(close(fd));
        LOGE(log << "Failed to stat file: " << filePath << ": " << strerror(statError));
        return false;
    }

    if (static_cast<uint64_t>(sb.st_size) > maxFileSize)
    {
        static_cast<void>(close(fd));
        LOGE(log << "File is too large: " << filePath);
        return false;
    }

    const auto fileSize = static_cast<size_t>(sb.st_size);
    if (fileSize == 0)
    {
        static_cast<void>(close(fd));
        LOGE(log << "Empty file: " << filePath);
        return false;
    }

    void *const mappedData = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mappedData == MAP_FAILED)
    {
        const int mapError = errno;
        static_cast<void>(close(fd));
        LOGE(log << "Failed to mmap file: " << filePath << ": " << strerror(mapError));
        return false;
    }

    // The mapping keeps the file alive, so the descriptor can be closed right
    // away. This way a buffer does not hold one descriptor per mapped file.
    static_cast<void>(close(fd));

    // Debug info is read in random order, so ask the kernel not to read ahead.
    static_cast<void>(madvise(mappedData, fileSize, MADV_RANDOM));

    m_mappedData = mappedData;
    m_fileSize = fileSize;
    return true;
}

void MemoryBuffer::Flush()
{
    if (m_mappedData == nullptr)
    {
        return;
    }

    // Drop the mapped pages, returning the memory to the kernel.
    // The pages are re-read from the file on the next access, and the mapping
    // address does not change, so Data() stays valid.
    if (madvise(m_mappedData, m_fileSize, MADV_DONTNEED) == -1)
    {
        const int adviseError = errno;
        LOGE(log << "Failed to release mapped memory back to the kernel: " << strerror(adviseError));
    }
}

MemoryBuffer::MemoryBuffer(MemoryBuffer &&other) noexcept
    : m_fileSize(std::exchange(other.m_fileSize, 0)),
      m_mappedData(std::exchange(other.m_mappedData, nullptr))
{
}

MemoryBuffer::~MemoryBuffer()
{
    Release();
}

void MemoryBuffer::Release() noexcept
{
    if (m_mappedData != nullptr)
    {
        static_cast<void>(munmap(m_mappedData, m_fileSize));
        m_mappedData = nullptr;
        m_fileSize = 0;
    }
}

MemoryBuffer &MemoryBuffer::operator=(MemoryBuffer &&other) noexcept
{
    if (this == std::addressof(other))
    {
        return *this;
    }

    // Clean up existing resources before taking ownership of new ones
    Release();

    m_mappedData = std::exchange(other.m_mappedData, nullptr);
    m_fileSize = std::exchange(other.m_fileSize, 0);
    return *this;
}

} // namespace dncdbg

#endif // FEATURE_PAL
