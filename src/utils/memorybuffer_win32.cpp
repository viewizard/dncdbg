// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/memorybuffer.h"
#include "utils/logger.h"
#include <cstdint>
#include <limits>

namespace dncdbg
{

bool MemoryBuffer::Open(const std::filesystem::path &filePath)
{
    m_fileHandle = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (m_fileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(m_fileHandle, &size))
    {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        LOGE(log << "Failed to get file size: " << filePath);
        return false;
    }

    // Limit file size to 4GB for consistency across platforms
    if (static_cast<uint64_t>(size.QuadPart) > std::numeric_limits<uint32_t>::max())
    {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        LOGE(log << "File is too large: " << filePath);
        return false;
    }

    m_fileSize = static_cast<size_t>(size.QuadPart);
    if (m_fileSize == 0)
    {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        LOGE(log << "Empty file: " << filePath);
        return false;
    }

    m_mappingHandle = CreateFileMappingW(m_fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (m_mappingHandle == nullptr)
    {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        LOGE(log << "Failed to create file mapping: " << filePath);
        return false;
    }

    m_mappedData = MapViewOfFile(m_mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (m_mappedData == nullptr)
    {
        CloseHandle(m_mappingHandle);
        m_mappingHandle = nullptr;
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        LOGE(log << "Failed to map view of file: " << filePath);
        return false;
    }

    return true;
}

MemoryBuffer::~MemoryBuffer()
{
    if (m_mappedData != nullptr)
    {
        UnmapViewOfFile(m_mappedData);
    }
    if (m_mappingHandle != nullptr)
    {
        CloseHandle(m_mappingHandle);
    }
    if (m_fileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_fileHandle);
    }
}

} // namespace dncdbg

#endif // _WIN32
