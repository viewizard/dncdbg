// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace DNCDbg
{
/// <summary>
/// Provides evaluation capabilities for expressions, particularly arithmetic operations.
/// This partial class works in conjunction with StackMachine.cs for expression evaluation.
/// </summary>
public partial class Evaluation
{
    #region Type Definitions

    // ============================================================================
    // WARNING: SYNCHRONIZATION REQUIRED WITH C++ CODE
    // ============================================================================
    // File: src/debugger/evalstackmachine.cpp
    // The following enums and dictionaries must be kept in sync with their C++
    // counterparts. The enum values are used for type identification across the
    // managed/unmanaged boundary. Changing values or order will break interop.
    // ============================================================================

    /// <summary>
    /// Maps primitive types to integer identifiers for interop with C++ code.
    /// </summary>
    /// <remarks>
    /// WARNING: Values must match BasicTypes enum in src/debugger/evalstackmachine.cpp.
    /// Note: Values start at 1 (not 0) to match C++ enum.
    /// </remarks>
    internal enum BasicTypes
    {
        TypeBoolean = 1,
        TypeByte,
        TypeSByte,
        TypeChar,
        TypeDouble,
        TypeSingle,
        TypeInt32,
        TypeUInt32,
        TypeInt64,
        TypeUInt64,
        TypeInt16,
        TypeUInt16,
        TypeString,
    }

    // WARNING: Keep this enum in sync with OperationType in src/debugger/evalstackmachine.cpp.
    internal enum OperationType
    {
        AddExpression = 1,
        SubtractExpression,
        MultiplyExpression,
        DivideExpression,
        ModuloExpression,
        RightShiftExpression,
        LeftShiftExpression,
        BitwiseNotExpression,
        LogicalAndExpression,
        LogicalOrExpression,
        ExclusiveOrExpression,
        BitwiseAndExpression,
        BitwiseOrExpression,
        LogicalNotExpression,
        EqualsExpression,
        NotEqualsExpression,
        LessThanExpression,
        GreaterThanExpression,
        LessThanOrEqualExpression,
        GreaterThanOrEqualExpression,
        UnaryPlusExpression,
        UnaryMinusExpression
    }

    /// <summary>
    /// Maps operation types to their corresponding C# operation functions.
    /// </summary>
    /// <remarks>
    /// WARNING: Keep this dictionary in sync with the operation implementations in
    /// src/debugger/evalstackmachine.cpp (CalculateTwoOperands and CalculateOneOperand).
    /// </remarks>
    internal static readonly Dictionary<OperationType, Func<object, object, object>> operationTypesMap =
        new Dictionary<OperationType, Func<object, object, object>>
        {
            { OperationType.AddExpression, AddExpression },
            { OperationType.DivideExpression, DivideExpression },
            { OperationType.MultiplyExpression, MultiplyExpression },
            { OperationType.ModuloExpression, ModuloExpression },
            { OperationType.SubtractExpression, SubtractExpression },
            { OperationType.RightShiftExpression, RightShiftExpression },
            { OperationType.LeftShiftExpression, LeftShiftExpression },
            { OperationType.BitwiseNotExpression, (firstOp, _) => BitwiseNotExpression(firstOp) },
            { OperationType.LogicalAndExpression, (firstOp, secondOp) => LogicalAndExpression(firstOp, secondOp) },
            { OperationType.LogicalOrExpression, (firstOp, secondOp) => LogicalOrExpression(firstOp, secondOp) },
            { OperationType.ExclusiveOrExpression, ExclusiveOrExpression },
            { OperationType.BitwiseAndExpression, BitwiseAndExpression },
            { OperationType.BitwiseOrExpression, BitwiseOrExpression },
            { OperationType.LogicalNotExpression, (firstOp, _) => LogicalNotExpression(firstOp) },
            { OperationType.EqualsExpression, (firstOp, secondOp) => EqualsExpression(firstOp, secondOp) },
            { OperationType.NotEqualsExpression, (firstOp, secondOp) => NotEqualsExpression(firstOp, secondOp) },
            { OperationType.LessThanExpression, (firstOp, secondOp) => LessThanExpression(firstOp, secondOp) },
            { OperationType.GreaterThanExpression, (firstOp, secondOp) => GreaterThanExpression(firstOp, secondOp) },
            { OperationType.LessThanOrEqualExpression, (firstOp, secondOp) => LessThanOrEqualExpression(firstOp, secondOp) },
            { OperationType.GreaterThanOrEqualExpression, (firstOp, secondOp) => GreaterThanOrEqualExpression(firstOp, secondOp) },
            { OperationType.UnaryPlusExpression, (firstOp, _) => UnaryPlusExpression(firstOp) },
            { OperationType.UnaryMinusExpression, (firstOp, _) => UnaryMinusExpression(firstOp) }
        };

    /// <summary>
    /// Maps BasicTypes to conversion functions from byte arrays.
    /// </summary>
    /// <remarks>
    /// WARNING: Keep this dictionary in sync with BasicTypesMap in src/debugger/evalstackmachine.cpp.
    /// </remarks>
    internal static readonly Dictionary<BasicTypes, Func<byte[], object>> typesMap =
        new Dictionary<BasicTypes, Func<byte[], object>>
        {
            { BasicTypes.TypeBoolean, values => BitConverter.ToBoolean(values, 0) },
            { BasicTypes.TypeByte, values => values[0] },
            { BasicTypes.TypeChar, values => BitConverter.ToChar(values, 0) },
            { BasicTypes.TypeDouble, values => BitConverter.ToDouble(values, 0) },
            { BasicTypes.TypeInt16, values => BitConverter.ToInt16(values, 0) },
            { BasicTypes.TypeInt32, values => BitConverter.ToInt32(values, 0) },
            { BasicTypes.TypeInt64, values => BitConverter.ToInt64(values, 0) },
            { BasicTypes.TypeSByte, values => (sbyte)values[0] },
            { BasicTypes.TypeSingle, values => BitConverter.ToSingle(values, 0) },
            { BasicTypes.TypeUInt16, values => BitConverter.ToUInt16(values, 0) },
            { BasicTypes.TypeUInt32, values => BitConverter.ToUInt32(values, 0) },
            { BasicTypes.TypeUInt64, values => BitConverter.ToUInt64(values, 0) }
        };

    /// <summary>
    /// Maps managed Type objects to BasicTypes for result type determination.
    /// </summary>
    /// <remarks>
    /// WARNING: Keep this dictionary in sync with the type mappings in src/debugger/evalstackmachine.cpp.
    /// </remarks>
    internal static readonly Dictionary<Type, BasicTypes> basicTypesMap = new Dictionary<Type, BasicTypes>
    {
        { typeof(Boolean), BasicTypes.TypeBoolean },
        { typeof(Byte), BasicTypes.TypeByte },
        { typeof(Char), BasicTypes.TypeChar },
        { typeof(Double), BasicTypes.TypeDouble },
        { typeof(Int16), BasicTypes.TypeInt16 },
        { typeof(Int32), BasicTypes.TypeInt32 },
        { typeof(Int64), BasicTypes.TypeInt64 },
        { typeof(SByte), BasicTypes.TypeSByte },
        { typeof(Single), BasicTypes.TypeSingle },
        { typeof(UInt16), BasicTypes.TypeUInt16 },
        { typeof(UInt32), BasicTypes.TypeUInt32 },
        { typeof(UInt64), BasicTypes.TypeUInt64 },
        { typeof(String), BasicTypes.TypeString }
    };

    /// <summary>
    /// Converts a managed value to an unmanaged pointer.
    /// </summary>
    /// <param name="value">The value to convert.</param>
    /// <returns>An unmanaged pointer to the value data.</returns>
    /// <remarks>
    /// WARNING: Keep this method in sync with value handling in src/debugger/evalstackmachine.cpp.
    /// The caller is responsible for freeing the returned pointer using appropriate interop functions:
    /// - SysFreeString (from interop.cpp) for strings
    /// - CoTaskMemFree (from interop.cpp) for other types
    /// </remarks>
    private static IntPtr ValueToPtr(object value)
    {
        if (value.GetType() == typeof(string))
            return Marshal.StringToBSTR(value as string);

        dynamic dynValue = value;
        byte[] bytes = BitConverter.GetBytes(dynValue);
        IntPtr ptr = Marshal.AllocCoTaskMem(bytes.Length);
        for (int i = 0; i < bytes.Length; i++)
        {
            Marshal.WriteByte(ptr, i, bytes[i]);
        }
        return ptr;
    }

    /// <summary>
    /// Converts an unmanaged pointer back to a managed object based on the specified type.
    /// </summary>
    /// <param name="ptr">Pointer to the unmanaged data.</param>
    /// <param name="type">The BasicType of the data.</param>
    /// <returns>The converted managed object.</returns>
    /// <exception cref="ArgumentException">Thrown when the BasicType is not supported.</exception>
    /// <remarks>
    /// WARNING: Keep this method in sync with GetValueByOperandDataType in src/debugger/evalstackmachine.cpp.
    /// </remarks>
    private static object PtrToValue(IntPtr ptr, int type)
    {
        var basicType = (BasicTypes)type;
        if (basicType == BasicTypes.TypeString)
        {
            if (ptr == IntPtr.Zero)
                return String.Empty;
            else
                return Marshal.PtrToStringBSTR(ptr);
        }

        if (!typesMap.TryGetValue(basicType, out var converter))
        {
            throw new ArgumentException($"Invalid BasicTypes value: {basicType}");
        }

        var intValue = Marshal.ReadInt64(ptr);
        var bytesArray = BitConverter.GetBytes(intValue);
        return converter(bytesArray);
    }

    /// <summary>
    /// Performs a calculation on two operands using the specified operation.
    /// </summary>
    /// <param name="firstOpPtr">Pointer to the first operand's data in unmanaged memory.</param>
    /// <param name="firstType">The type of the first operand (BasicTypes).</param>
    /// <param name="secondOpPtr">Pointer to the second operand's data in unmanaged memory.</param>
    /// <param name="secondType">The type of the second operand (BasicTypes).</param>
    /// <param name="operation">The operation to perform (OperationType).</param>
    /// <param name="resultType">The resulting type (BasicTypes) on success.</param>
    /// <param name="result">
    /// Pointer to the result in unmanaged memory. The caller must free this using:
    /// - SysFreeString (from interop.cpp) for string results (BasicTypes.TypeString)
    /// - CoTaskMemFree (from interop.cpp) for other primitive types.
    /// </param>
    /// <param name="errorText">
    /// Error message if the operation fails. The caller must free this using SysFreeString.
    /// </param>
    /// <returns>
    /// RetCode.OK on success; 
    /// RetCode.Fail for invalid parameters (unknown operation type or unsupported operand/result type);
    /// RetCode.Exception for errors during execution (e.g., arithmetic overflow, invalid cast).
    /// </returns>
    /// <remarks>
    /// WARNING: This method is called from C++ (see src/debugger/evalstackmachine.cpp via CalculationDelegate).
    /// The signature and error handling must remain consistent with the C++ side.
    /// </remarks>
    internal static RetCode Calculation(IntPtr firstOpPtr, int firstType, IntPtr secondOpPtr, int secondType,
                                        int operation, out int resultType, out IntPtr result, out IntPtr errorText)
    {
        resultType = 0;
        result = IntPtr.Zero;
        errorText = IntPtr.Zero;

        // Validate operation code
        if (!Enum.IsDefined(typeof(OperationType), operation))
        {
            errorText = Marshal.StringToBSTR("error: invalid operation code");
            return RetCode.Fail;
        }
        var op = (OperationType)operation;

        // Validate operand types (strings are handled specially, others must be in typesMap)
        var firstBasic = (BasicTypes)firstType;
        var secondBasic = (BasicTypes)secondType;
        if (firstBasic != BasicTypes.TypeString && !typesMap.ContainsKey(firstBasic))
        {
            errorText = Marshal.StringToBSTR("error: invalid first operand type");
            return RetCode.Fail;
        }
        if (secondBasic != BasicTypes.TypeString && !typesMap.ContainsKey(secondBasic))
        {
            errorText = Marshal.StringToBSTR("error: invalid second operand type");
            return RetCode.Fail;
        }

        // Ensure the operation is supported
        if (!operationTypesMap.TryGetValue(op, out var opFunc))
        {
            errorText = Marshal.StringToBSTR("error: operation not supported");
            return RetCode.Fail;
        }

        try
        {
            var firstOp = PtrToValue(firstOpPtr, firstType);
            var secondOp = PtrToValue(secondOpPtr, secondType);
            object operationResult = opFunc(firstOp, secondOp);
            if (!basicTypesMap.TryGetValue(operationResult.GetType(), out var resultBasic))
            {
                errorText = Marshal.StringToBSTR("error: result type not supported");
                return RetCode.Fail;
            }
            resultType = (int)resultBasic;
            result = ValueToPtr(operationResult);
        }
        catch (Exception ex)
        {
            errorText = Marshal.StringToBSTR("error: " + ex.Message);
            return RetCode.Exception;
        }

        return RetCode.OK;
    }

    private static object AddExpression(dynamic first, dynamic second)
    {
        return first + second;
    }

    private static object SubtractExpression(dynamic first, dynamic second)
    {
        return first - second;
    }

    private static object MultiplyExpression(dynamic first, dynamic second)
    {
        return first * second;
    }

    private static object DivideExpression(dynamic first, dynamic second)
    {
        return first / second;
    }

    private static object ModuloExpression(dynamic first, dynamic second)
    {
        return first % second;
    }

    private static object RightShiftExpression(dynamic first, dynamic second)
    {
        return first >> second;
    }

    private static object LeftShiftExpression(dynamic first, dynamic second)
    {
        return first << second;
    }

    private static object BitwiseNotExpression(dynamic first)
    {
        return ~first;
    }

    private static object ExclusiveOrExpression(dynamic first, dynamic second)
    {
        return first ^ second;
    }

    private static object BitwiseAndExpression(dynamic first, dynamic second)
    {
        return first & second;
    }

    private static object BitwiseOrExpression(dynamic first, dynamic second)
    {
        return first | second;
    }

    private static bool LogicalAndExpression(dynamic first, dynamic second)
    {
        return first && second;
    }

    private static bool LogicalOrExpression(dynamic first, dynamic second)
    {
        return first || second;
    }

    private static object LogicalNotExpression(dynamic first)
    {
        return !first;
    }

    private static bool EqualsExpression(dynamic first, dynamic second)
    {
        return first == second;
    }

    private static bool NotEqualsExpression(dynamic first, dynamic second)
    {
        return first != second;
    }

    private static bool LessThanExpression(dynamic first, dynamic second)
    {
        return first < second;
    }

    private static bool GreaterThanExpression(dynamic first, dynamic second)
    {
        return first > second;
    }

    private static bool LessThanOrEqualExpression(dynamic first, dynamic second)
    {
        return first <= second;
    }

    private static bool GreaterThanOrEqualExpression(dynamic first, dynamic second)
    {
        return first >= second;
    }

    private static object UnaryPlusExpression(dynamic first)
    {
        return +first;
    }

    private static object UnaryMinusExpression(dynamic first)
    {
        return -first;
    }

    #endregion // Type Definitions
}
}
