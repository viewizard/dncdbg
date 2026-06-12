// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if (defined(__APPLE__) && defined(__MACH__))

#include "utils/utftoupper.h"
#include "utils/logger.h"
#include <CoreFoundation/CoreFoundation.h>

namespace dncdbg
{

std::string to_uppercase(const std::string &input)
{
    if (input.empty())
    {
        return {};
    }

    // Step 1: Create CFString from UTF-8 input
    CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(input.data()),
                                                static_cast<CFIndex>(input.size()), kCFStringEncodingUTF8, FALSE);

    if (cfStr == nullptr)
    {
        LOGE(log << "CFStringCreateWithBytes failed for UTF-8 input");
        return input;
    }

    // Step 2: Create mutable copy for in-place modification
    CFMutableStringRef cfMutableStr = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cfStr);
    CFRelease(cfStr);

    if (cfMutableStr == nullptr)
    {
        LOGE(log << "CFStringCreateMutableCopy failed");
        return input;
    }

    // Step 3: Convert to uppercase using current locale
    CFLocaleRef locale = CFLocaleCopyCurrent();
    CFStringUppercase(cfMutableStr, locale);
    CFRelease(locale);

    // Step 4: Convert back to UTF-8
    const CFIndex length = CFStringGetLength(cfMutableStr);
    const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

    if (maxSize <= 0)
    {
        LOGE(log << "CFStringGetMaximumSizeForEncoding returned invalid size");
        CFRelease(cfMutableStr);
        return input;
    }

    std::string result(maxSize, '\0');
    CFIndex bytesUsed = 0;
    const CFIndex bytesResult = CFStringGetBytes(cfMutableStr, CFRangeMake(0, length), kCFStringEncodingUTF8, 0, FALSE,
                                                 reinterpret_cast<UInt8 *>(result.data()), maxSize, &bytesUsed);

    CFRelease(cfMutableStr);

    if (bytesResult == 0 || bytesUsed <= 0)
    {
        LOGE(log << "CFStringGetBytes failed to convert back to UTF-8");
        return input;
    }

    result.resize(bytesUsed);
    return result;
}

} // namespace dncdbg

#endif // (defined(__APPLE__) && defined(__MACH__))
