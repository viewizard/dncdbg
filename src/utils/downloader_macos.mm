// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#if (defined(__APPLE__) && defined(__MACH__))

#include "utils/downloader.h"
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <utility>

namespace dncdbg
{

namespace
{

constexpr NSTimeInterval requestTimeoutSeconds = 60.0;
constexpr NSInteger httpStatusOk = 200;
constexpr NSInteger httpStatusMultipleChoices = 300;

std::string NSStringToString(NSString *value)
{
    const char *utf8Value = value.UTF8String;
    return utf8Value == nullptr ? std::string() : std::string(utf8Value);
}

} // namespace

bool DownloadSource(const std::string &urlStr, std::string &output)
{
    output.clear();

    if (urlStr.empty())
    {
        output = "URL must not be empty";
        return false;
    }

    @autoreleasepool
    {
        NSString *const urlString = [NSString stringWithUTF8String:urlStr.c_str()];
        if (urlString == nil)
        {
            output = "URL is not valid UTF-8";
            return false;
        }

        NSURL *const url = [NSURL URLWithString:urlString];
        if (url == nil)
        {
            output = "URL must be an absolute HTTP or HTTPS URL";
            return false;
        }

        NSString *const scheme = url.scheme.lowercaseString;
        if (url.host == nil ||
            (![scheme isEqualToString:@"http"] && ![scheme isEqualToString:@"https"]))
        {
            output = "URL must be an absolute HTTP or HTTPS URL";
            return false;
        }

        NSURLSessionConfiguration *const configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = requestTimeoutSeconds;
        configuration.timeoutIntervalForResource = requestTimeoutSeconds;

        // dispatch_semaphore_create() fails only when the system is out of memory.
        dispatch_semaphore_t const semaphore = dispatch_semaphore_create(0);
        if (semaphore == nullptr)
        {
            output = "Failed to create the completion semaphore";
            return false;
        }

        NSURLSession *const session = [NSURLSession sessionWithConfiguration:configuration];
        __block bool succeeded = false;
        __block std::string downloadedData;
        __block std::string errorMessage;

        NSURLSessionDataTask *const task = [session dataTaskWithURL:url
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error)
            {
                if (error != nil)
                {
                    errorMessage = "Request error: " + NSStringToString(error.localizedDescription);
                }
                else if (![response isKindOfClass:[NSHTTPURLResponse class]])
                {
                    errorMessage = "Response is not an HTTP response";
                }
                else
                {
                    const NSInteger statusCode = [(NSHTTPURLResponse *)response statusCode];
                    if (statusCode < httpStatusOk || statusCode >= httpStatusMultipleChoices)
                    {
                        errorMessage = "HTTP request failed with status code " + std::to_string(statusCode);
                    }
                    else
                    {
                        const NSUInteger length = data.length;
                        if (length != 0)
                        {
                            downloadedData.assign(static_cast<const char *>(data.bytes), length);
                        }
                        succeeded = true;
                    }
                }

                dispatch_semaphore_signal(semaphore);
            }];

        [task resume];
        // DownloadSource is intentionally synchronous because the downloaded data is returned to the caller.
        // NOLINTNEXTLINE(clang-analyzer-optin.performance.GCDAntipattern)
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        [session invalidateAndCancel];
        // Without ARC, dispatch_semaphore_create() returns a +1 reference that must be balanced.
        dispatch_release(semaphore);

        if (succeeded)
        {
            output = std::move(downloadedData);
            return true;
        }

        if (errorMessage.empty())
        {
            output = "Request failed";
        }
        else
        {
            output = std::move(errorMessage);
        }
        return false;
    }
}

} // namespace dncdbg

#endif // (defined(__APPLE__) && defined(__MACH__))
