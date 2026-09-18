// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_DOWNLOADER_H
#define UTILS_DOWNLOADER_H

#include <string>

namespace dncdbg
{

// Downloads the contents of an HTTP or HTTPS resource into `output`.
// Returns `true` on success. On failure, returns `false` and `output`
// contains a human-readable error message.
bool DownloadSource(const std::string &urlStr, std::string &output);

} // namespace dncdbg

#endif // UTILS_DOWNLOADER_H
