// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#pragma warning(disable : 4068) // Visual Studio should ignore GCC pragmas
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <cor.h>
#include <cordebug.h>
#pragma GCC diagnostic pop

#include <string>

namespace dncdbg
{

HRESULT PrintValue(ICorDebugValue *pInputValue, std::string &output, bool escape = true);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue);
HRESULT PrintNullableValue(ICorDebugValue *pValue, std::string &outTextValue);
HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output);
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull = nullptr);

} // namespace dncdbg
