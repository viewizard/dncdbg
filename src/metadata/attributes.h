// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_ATTRIBUTES_H
#define METADATA_ATTRIBUTES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string_view>
#include <string>
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
    // https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerbrowsableattribute
    // Determines if and how a member is displayed in the debugger variable windows.
    static constexpr std::string_view Browsable = "System.Diagnostics.DebuggerBrowsableAttribute..ctor";
    // https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggertypeproxyattribute
    // Specifies the display proxy for a type.
    static constexpr std::string_view TypeProxy = "System.Diagnostics.DebuggerTypeProxyAttribute..ctor";
};

// https://github.com/dotnet/runtime/blob/737dcdda62ca847173ab50c905cd1604e70633b9/src/libraries/System.Private.CoreLib/src/System/Diagnostics/DebuggerBrowsableAttribute.cs#L16
enum class DebuggerBrowsableState : uint32_t // NOLINT(performance-enum-size)
{
    Never = 0,
    Expanded = 1,
    Collapsed = 2,
    RootHidden = 3
};

bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string_view attrName);
bool HasAttribute(IMetaDataImport *pMDImport, mdToken tok, const std::vector<std::string_view> &attrNames);
DebuggerBrowsableState GetDebuggerBrowsableAttributeState(IMetaDataImport *pMDImport, mdToken tok);
bool HasDebuggerTypeProxyAttribute(IMetaDataImport *pMDImport, mdToken tok, std::string &proxyTypeName);
bool HasAssemblyDebuggerTypeProxyAttribute(IMetaDataImport *pMDImport, mdToken tok, const std::string &detectTypeName, std::string &proxyTypeName);

} // namespace dncdbg

#endif // METADATA_ATTRIBUTES_H
