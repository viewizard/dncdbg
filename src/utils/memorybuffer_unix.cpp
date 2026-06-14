// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef FEATURE_PAL

#include "utils/memorybuffer.h"
#include "utils/logger.h"
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dncdbg
{

bool MemoryBuffer::Open(const std::filesystem::path &filePath)
{
    m_fd = open(filePath.c_str(), O_RDONLY); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (m_fd == -1)
    {
        return false;
    }

    struct stat sb{};
    if (fstat(m_fd, &sb) == -1)
    {
        close(m_fd);
        m_fd = -1;
        LOGE(log << "Failed to stat file: " << filePath);
        return false;
    }

    // Limit file size to 4GB for consistency across platforms
    if (static_cast<uint64_t>(sb.st_size) > std::numeric_limits<uint32_t>::max())
    {
        close(m_fd);
        m_fd = -1;
        LOGE(log << "File is too large: " << filePath);
        return false;
    }

    m_fileSize = static_cast<size_t>(sb.st_size);
    if (m_fileSize == 0)
    {
        close(m_fd);
        m_fd = -1;
        LOGE(log << "Empty file: " << filePath);
        return false;
    }

    m_mappedData = mmap(nullptr, m_fileSize, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (m_mappedData == MAP_FAILED)
    {
        m_mappedData = nullptr;
        close(m_fd);
        m_fd = -1;
        LOGE(log << "Failed to mmap file: " << filePath);
        return false;
    }

    madvise(m_mappedData, m_fileSize, MADV_RANDOM);
    return true;
}

MemoryBuffer::~MemoryBuffer()
{
    if (m_mappedData != nullptr)
    {
        munmap(m_mappedData, m_fileSize);
    }
    if (m_fd != -1)
    {
        close(m_fd);
    }
}

} // namespace dncdbg

#endif // FEATURE_PAL
