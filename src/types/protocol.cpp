// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "types/protocol.h"
#include "utils/filesystem.h"

// Important! All "types" code must not depends from other debugger's code.

namespace dncdbg
{

Source::Source(const std::string &path)
    : name(GetFileName(path)),
      path(path)
{
}

} // namespace dncdbg
