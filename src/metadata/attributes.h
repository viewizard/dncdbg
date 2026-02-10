// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include <string_view>
#include <vector>

namespace dncdbg
{

struct DebuggerAttribute
{
    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggernonusercodeattribute
    // This attribute suppresses the display of these adjunct types and members in the debugger window and
    // automatically steps through, rather than into, designer provided code.
    static constexpr std::string_view NonUserCode = "System.Diagnostics.DebuggerNonUserCodeAttribute..ctor";
    // Check `DebuggerStepThroughAttribute` for method and class.
    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerstepthroughattribute
    // Instructs the debugger to step through the code instead of stepping into the code.
    static constexpr std::string_view StepThrough = "System.Diagnostics.DebuggerStepThroughAttribute..ctor";
    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerhiddenattribute
    // ... debugger does not stop in a method marked with this attribute and does not allow a breakpoint to be set in the method.
    // https://docs.microsoft.com/en-us/dotnet/visual-basic/misc/bc40051
    // System.Diagnostics.DebuggerHiddenAttribute does not affect 'Get' or 'Set' when applied to the Property definition.
    // Apply the attribute directly to the 'Get' and 'Set' procedures as appropriate.
    static constexpr std::string_view Hidden = "System.Diagnostics.DebuggerHiddenAttribute..ctor";
};

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const std::string_view &attrName);
bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const std::vector<std::string_view> &attrNames);

} // namespace dncdbg
