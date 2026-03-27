// Copyright (c) 2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Runtime.InteropServices;

namespace DNCDbg
{
/// <summary>
/// Return codes for interop methods.
/// </summary>
/// <remarks>
/// WARNING: Must match the RetCode enum in src/managed/interop.cpp.
/// Values are used for communication between managed and unmanaged code.
/// </remarks>
internal enum RetCode : int
{
    OK = 0,
    Fail = 1,
    Exception = 2
}

/// <summary>
/// Utility class providing interop helper methods for memory and string management.
/// </summary>
/// <remarks>
/// This class provides managed wrappers for common operations that need to be
/// coordinated with the C++ side via delegates defined in interop.cpp.
/// </remarks>
public static class Utils
{
    #region String and Memory Operations

    // ============================================================================
    // WARNING: SYNCHRONIZATION REQUIRED WITH C++ CODE
    // ============================================================================
    // File: src/managed/interop.cpp
    // The following methods are called from C++ via delegates. Their signatures
    // and behavior must match the C++ delegate definitions exactly.
    // ============================================================================

    /// <summary>
    /// Converts the source string to uppercase using invariant culture and returns it as a BSTR.
    /// The caller is responsible for freeing the returned BSTR using SysFreeString.
    /// Corresponds to StringToUpperDelegate in interop.cpp.
    /// </summary>
    /// <param name="srcString">Source string (may be null).</param>
    /// <param name="dstString">Output BSTR that must be freed by caller.</param>
    /// <returns>RetCode.OK on success.</returns>
    internal static RetCode StringToUpper([MarshalAs(UnmanagedType.LPWStr)] string srcString, out IntPtr dstString)
    {
        dstString = IntPtr.Zero;

        if (srcString is null)
        {
            return RetCode.Fail;
        }

        try
        {
            // Use invariant culture to avoid culture-specific casing issues.
            dstString = Marshal.StringToBSTR(srcString.ToUpperInvariant());
        }
        catch
        {
            return RetCode.Exception;
        }

        return RetCode.OK;
    }

    /// <summary>
    /// Allocates a BSTR of the specified length, filled with null characters.
    /// Corresponds to SysAllocStringLenDelegate in interop.cpp.
    /// </summary>
    /// <param name="size">Number of characters to allocate. Must be non-negative.</param>
    /// <returns>Pointer to the allocated BSTR, or IntPtr.Zero if size is negative or allocation fails.</returns>
    internal static IntPtr SysAllocStringLen(int size)
    {
        if (size < 0)
        {
            return IntPtr.Zero;
        }

        try
        {
            string empty = new string('\0', size);
            return Marshal.StringToBSTR(empty);
        }
        catch
        {
            return IntPtr.Zero;
        }
    }

    /// <summary>
    /// Frees a BSTR allocated by SysAllocStringLen or StringToUpper.
    /// Corresponds to SysFreeStringDelegate in interop.cpp.
    /// </summary>
    /// <param name="ptr">Pointer to BSTR to free. Can be IntPtr.Zero.</param>
    internal static void SysFreeString(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
        {
            return;
        }

        Marshal.FreeBSTR(ptr);
    }

    /// <summary>
    /// Frees memory allocated by CoTaskMemAlloc or similar.
    /// Corresponds to CoTaskMemFreeDelegate in interop.cpp.
    /// </summary>
    /// <param name="ptr">Pointer to memory to free. Can be IntPtr.Zero.</param>
    internal static void CoTaskMemFree(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
        {
            return;
        }

        Marshal.FreeCoTaskMem(ptr);
    }

    #endregion // String and Memory Operations
}
}
