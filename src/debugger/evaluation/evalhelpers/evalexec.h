// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALEXEC_H
#define DEBUGGER_EVALUATION_EVALEXEC_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <string>
#include <vector>

namespace dncdbg::EvalExec
{

HRESULT CallFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType *pArgType,
                     std::vector<ToRelease<ICorDebugType>> *pTrMethodGenericTypes, ICorDebugValue **ppArgsValue,
                     uint32_t argsValueCount, FormatSpecifier specifier, ICorDebugValue **ppEvalResult);

HRESULT CallConstructor(ICorDebugThread *pThread, ICorDebugFunction *pConstrFunc, std::vector<ToRelease<ICorDebugType>> &trTypeParams,
                        ICorDebugValue **ppArgsValue, uint32_t argsValueCount, ICorDebugValue **ppEvalResult);

HRESULT CreateTypeObject(ICorDebugThread *pThread, ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult = nullptr);

HRESULT CreateArray(ICorDebugThread *pThread, ICorDebugType *pElementType,
                    std::vector<uint32_t> &dimensions, ICorDebugValue **ppEvalResult);

HRESULT CreateLiteralFieldValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, UVCP_CONSTANT pRawValue,
                                ULONG rawValueLength, ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName);

HRESULT CreateLiteralLocalValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName);

HRESULT CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString);

HRESULT CreateValueType(ICorDebugThread *pThread, ICorDebugClass *pValueTypeClass, void *valueData, ICorDebugValue **ppValue);

// Cleans up the EvalExec internal state. See ManagedDebugger::Cleanup().
void Cleanup();

[[nodiscard]] uint32_t GetEvalFlags();
void SetEvalFlags(uint32_t evalFlags);

} // namespace dncdbg::EvalExec

#endif // DEBUGGER_EVALUATION_EVALEXEC_H
