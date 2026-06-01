// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_HRESULT_H
#define UTILS_HRESULT_H

#ifdef FEATURE_PAL
#include <pal_mstypes.h>
#else
#include <winerror.h>
#endif

namespace dncdbg
{

#ifndef IfFailRet
#define IfFailRet(EXPR) do { Status = (EXPR); if(FAILED(Status)) { return (Status); } } while (0) // NOLINT(cppcoreguidelines-macro-usage)
#endif

constexpr HRESULT S_CAN_EXIT = 0x00777001L;
constexpr HRESULT S_NO_STATIC = 0x00777002L;
constexpr HRESULT S_USE_SIMPLE_STEPPER = 0x00777003L;
constexpr HRESULT S_IGNORE = 0x00777004L;
constexpr HRESULT S_SKIP = 0x00777005L;

} // namespace dncdbg

#endif // UTILS_HRESULT_H
