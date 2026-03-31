// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_LIMITS_H
#define UTILS_LIMITS_H

// avoid bug with limit.h and clang-6.x
#ifdef __linux__
#include <linux/limits.h>
#endif

// include system <limits.h> file
#include <../include/limits.h>

// define LINE_MAX for windows
#ifndef LINE_MAX
#define LINE_MAX (2048)
#endif

#endif // UTILS_LIMITS_H
