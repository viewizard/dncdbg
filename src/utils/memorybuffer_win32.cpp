// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _WIN32

#include "utils/memorybuffer.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <windows.h>

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

    const WSTRING wFilePath = to_utf16(filePath);
    // FILE_SHARE_WRITE and FILE_SHARE_DELETE let a build process rewrite or
    // replace the file while the debugger keeps it mapped, instead of failing
    // with a sharing violation. FILE_FLAG_RANDOM_ACCESS disables read-ahead,
    // since debug info is read in random order.
    const HANDLE fileHandle = CreateFileW(wFilePath.c_str(), GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(fileHandle, &size))
    {
        const DWORD sizeError = GetLastError();
        static_cast<void>(CloseHandle(fileHandle));
        LOGE(log << "Failed to get file size: " << filePath << ", error=" << sizeError);
        return false;
    }

    if (static_cast<uint64_t>(size.QuadPart) > maxFileSize)
    {
        static_cast<void>(CloseHandle(fileHandle));
        LOGE(log << "File is too large: " << filePath);
        return false;
    }

    const auto fileSize = static_cast<size_t>(size.QuadPart);
    if (fileSize == 0)
    {
        static_cast<void>(CloseHandle(fileHandle));
        LOGE(log << "Empty file: " << filePath);
        return false;
    }

    const HANDLE mappingHandle = CreateFileMappingW(fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const DWORD mappingError = GetLastError();
    // The mapping keeps the file alive, so the file handle can be closed right
    // away. This way a buffer does not hold one handle per mapped file.
    static_cast<void>(CloseHandle(fileHandle));
    if (mappingHandle == nullptr)
    {
        LOGE(log << "Failed to create file mapping: " << filePath << ", error=" << mappingError);
        return false;
    }

    void *const mappedData = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (mappedData == nullptr)
    {
        const DWORD mapError = GetLastError();
        static_cast<void>(CloseHandle(mappingHandle));
        LOGE(log << "Failed to map view of file: " << filePath << ", error=" << mapError);
        return false;
    }

    m_mappingHandle = mappingHandle;
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

    // Windows has no direct equivalent of madvise(MADV_DONTNEED), so the view
    // is recreated to release its pages back to the system.
    if (!UnmapViewOfFile(m_mappedData))
    {
        const DWORD unmapError = GetLastError();
        LOGE(log << "Failed to unmap view of file, error=" << unmapError);
        return;
    }

    m_mappedData = MapViewOfFile(m_mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (m_mappedData == nullptr)
    {
        // Release all resources so the object returns to a closed state and can be reopened.
        const DWORD mapError = GetLastError();
        m_fileSize = 0;
        static_cast<void>(CloseHandle(m_mappingHandle));
        m_mappingHandle = nullptr;
        LOGE(log << "Failed to map view of file, error=" << mapError);
    }
}

MemoryBuffer::MemoryBuffer(MemoryBuffer &&other) noexcept
    : m_fileSize(std::exchange(other.m_fileSize, 0)),
      m_mappedData(std::exchange(other.m_mappedData, nullptr)),
      m_mappingHandle(std::exchange(other.m_mappingHandle, nullptr))
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
        static_cast<void>(UnmapViewOfFile(m_mappedData));
        m_mappedData = nullptr;
        m_fileSize = 0;
    }
    if (m_mappingHandle != nullptr)
    {
        static_cast<void>(CloseHandle(m_mappingHandle));
        m_mappingHandle = nullptr;
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
    m_mappingHandle = std::exchange(other.m_mappingHandle, nullptr);
    return *this;
}

} // namespace dncdbg

#endif // _WIN32
