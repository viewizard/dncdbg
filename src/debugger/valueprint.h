// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#include <cor.h>
#include <cordebug.h>

#include <string>

namespace dncdbg
{

HRESULT PrintValue(ICorDebugValue *pInputValue, std::string &output, bool escape = true);
HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue);
HRESULT PrintNullableValue(ICorDebugValue *pValue, std::string &outTextValue);
HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output);
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull = nullptr);

} // namespace dncdbg
