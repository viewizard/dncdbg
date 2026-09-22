// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_SYSTEMTYPES_H
#define DEBUGGER_EVALUATION_EVALHELPERS_SYSTEMTYPES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

namespace dncdbg::SystemTypes
{

enum class SystemType : uint8_t
{
    Void,
    Boolean,
    Char,
    SByte,
    Byte,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Single,
    Double,
    IntPtr,
    UIntPtr,
    Decimal,
    Array,
    Enum,
    size
};

// Get the cached ICorDebugClass for a system type. The reference is returned with an incremented
// reference count (the caller is responsible for releasing it).
HRESULT GetClass(SystemType systemType, ICorDebugClass **ppClass);
// Same as GetClass(SystemType, ICorDebugClass **), but resolves a built-in element type
// (e.g. ELEMENT_TYPE_I4) to the corresponding system type (e.g. SystemType::Int32) first.
HRESULT GetClass(CorElementType elemType, ICorDebugClass **ppClass);
// Find ICorDebugClass objects for all system types needed by the stack machine during
// System.Private.CoreLib load. See ManagedCallback::LoadModule().
HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
// Release all cached classes. See ManagedDebugger::Cleanup().
void Cleanup();

} // namespace dncdbg::SystemTypes

#endif // DEBUGGER_EVALUATION_EVALHELPERS_SYSTEMTYPES_H
