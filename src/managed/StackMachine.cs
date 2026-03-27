// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using System.Text;

namespace DNCDbg
{
public partial class Evaluation
{
    #region Blittable Types

    // ============================================================================
    // WARNING: SYNCHRONIZATION REQUIRED WITH C++ CODE
    // ============================================================================
    // File: src/debugger/evalstackmachine.cpp
    // These blittable structs must match the C++ expectations exactly for proper
    // marshaling between managed and unmanaged code. Any changes to layout, size,
    // or field types must be synchronized with the C++ implementation.
    // ============================================================================

    /// <summary>
    /// Blittable character struct for interop with C++ code.
    /// Represents a 2-byte Unicode character matching C++ wchar_t expectations.
    /// </summary>
    /// <remarks>
    /// WARNING: Must be kept in sync with C++ code in src/debugger/evalstackmachine.cpp.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct BlittableChar
    {
        public char Value;

        public static explicit operator BlittableChar(char value)
        {
            return new BlittableChar { Value = value };
        }

        public static implicit operator char (BlittableChar value)
        {
            return value.Value;
        }
    }

    // WARNING: This struct layout must match C++ expectations. See src/debugger/evalstackmachine.cpp
    public struct BlittableBoolean
    {
        private byte byteValue;

        public bool Value
        {
            get => byteValue != 0;
            set => byteValue = (byte)(value ? 1 : 0);
        }

        public static explicit operator BlittableBoolean(bool value)
        {
            return new BlittableBoolean { Value = value };
        }

        public static implicit operator bool (BlittableBoolean value)
        {
            return value.Value;
        }
    }

    #endregion

    #region Value Marshaling

    /// <summary>
    /// Marshals a value to unmanaged memory for interop with C++ code.
    /// </summary>
    /// <param name="value">The value to marshal.</param>
    /// <param name="size">Output: size of the marshaled data.</param>
    /// <param name="data">Output: pointer to unmanaged memory.</param>
    /// <remarks>
    /// WARNING: Memory management is critical here. The caller is responsible for freeing
    /// the returned data pointer using Marshal.FreeCoTaskMem or the appropriate method.
    /// </remarks>
    private static void MarshalValue(object value, out int size, out IntPtr data)
    {
        if (value is string stringValue)
        {
            data = Marshal.StringToBSTR(stringValue);
            size = 0;
        }
        else if (value is char charValue)
        {
            size = Marshal.SizeOf<BlittableChar>();
            data = Marshal.AllocCoTaskMem(size);
            try
            {
                Marshal.StructureToPtr((BlittableChar)charValue, data, false);
            }
            catch
            {
                Marshal.FreeCoTaskMem(data);
                data = IntPtr.Zero;
                size = 0;
                throw;
            }
        }
        else if (value is bool boolValue)
        {
            size = Marshal.SizeOf<BlittableBoolean>();
            data = Marshal.AllocCoTaskMem(size);
            try
            {
                Marshal.StructureToPtr((BlittableBoolean)boolValue, data, false);
            }
            catch
            {
                Marshal.FreeCoTaskMem(data);
                data = IntPtr.Zero;
                size = 0;
                throw;
            }
        }
        else
        {
            size = Marshal.SizeOf(value);
            data = Marshal.AllocCoTaskMem(size);
            try
            {
                Marshal.StructureToPtr(value, data, false);
            }
            catch
            {
                Marshal.FreeCoTaskMem(data);
                data = IntPtr.Zero;
                size = 0;
                throw;
            }
        }
    }

    #endregion

    #region Type Definitions

    // ============================================================================
    // WARNING: SYNCHRONIZATION REQUIRED WITH C++ CODE
    // ============================================================================
    // File: src/debugger/evalstackmachine.cpp
    // The following enums and dictionaries must be kept in sync with their C++
    // counterparts. The ePredefinedType enum values are used as indices into
    // BasicTypesAlias arrays in the C++ code. The eOpCode enum values are used
    // as indices into the CommandImplementation array.
    // ============================================================================

    /// <summary>
    /// Maps C# predefined type keywords to internal type identifiers.
    /// </summary>
    /// <remarks>
    /// WARNING: Order and values must match BasicTypesAlias arrays in src/debugger/evalstackmachine.cpp.
    /// Used as array indices - DO NOT change order or values.
    /// </remarks>
    internal enum ePredefinedType
    {
        BoolKeyword,
        ByteKeyword,
        CharKeyword,
        DecimalKeyword,
        DoubleKeyword,
        FloatKeyword,
        IntKeyword,
        LongKeyword,
        ObjectKeyword,
        SByteKeyword,
        ShortKeyword,
        StringKeyword,
        UShortKeyword,
        UIntKeyword,
        ULongKeyword
    }

    // WARNING: This dictionary maps .NET types to ePredefinedType. The ePredefinedType values must be kept in sync
    // with the BasicTypesAlias arrays in src/debugger/evalstackmachine.cpp.
    static readonly Dictionary<Type, ePredefinedType> TypeAlias = new Dictionary<Type, ePredefinedType>
    {
        { typeof(bool), ePredefinedType.BoolKeyword },
        { typeof(byte), ePredefinedType.ByteKeyword },
        { typeof(char), ePredefinedType.CharKeyword },
        { typeof(decimal), ePredefinedType.DecimalKeyword },
        { typeof(double), ePredefinedType.DoubleKeyword },
        { typeof(float), ePredefinedType.FloatKeyword },
        { typeof(int), ePredefinedType.IntKeyword },
        { typeof(long), ePredefinedType.LongKeyword },
        { typeof(object), ePredefinedType.ObjectKeyword },
        { typeof(sbyte), ePredefinedType.SByteKeyword },
        { typeof(short), ePredefinedType.ShortKeyword },
        { typeof(string), ePredefinedType.StringKeyword },
        { typeof(ushort), ePredefinedType.UShortKeyword },
        { typeof(uint), ePredefinedType.UIntKeyword },
        { typeof(ulong), ePredefinedType.ULongKeyword }
    };

    // WARNING: This dictionary maps Roslyn SyntaxKind to ePredefinedType. Keep in sync with ePredefinedType enum
    // and BasicTypesAlias in src/debugger/evalstackmachine.cpp.
    static readonly Dictionary<SyntaxKind, ePredefinedType> TypeKindAlias = new Dictionary<SyntaxKind, ePredefinedType>
    {
        { SyntaxKind.BoolKeyword,    ePredefinedType.BoolKeyword },
        { SyntaxKind.ByteKeyword,    ePredefinedType.ByteKeyword },
        { SyntaxKind.CharKeyword,    ePredefinedType.CharKeyword },
        { SyntaxKind.DecimalKeyword, ePredefinedType.DecimalKeyword },
        { SyntaxKind.DoubleKeyword,  ePredefinedType.DoubleKeyword },
        { SyntaxKind.FloatKeyword,   ePredefinedType.FloatKeyword },
        { SyntaxKind.IntKeyword,     ePredefinedType.IntKeyword },
        { SyntaxKind.LongKeyword,    ePredefinedType.LongKeyword },
        { SyntaxKind.ObjectKeyword,  ePredefinedType.ObjectKeyword },
        { SyntaxKind.SByteKeyword,   ePredefinedType.SByteKeyword },
        { SyntaxKind.ShortKeyword,   ePredefinedType.ShortKeyword },
        { SyntaxKind.StringKeyword,  ePredefinedType.StringKeyword },
        { SyntaxKind.UShortKeyword,  ePredefinedType.UShortKeyword },
        { SyntaxKind.UIntKeyword,    ePredefinedType.UIntKeyword },
        { SyntaxKind.ULongKeyword,   ePredefinedType.ULongKeyword }
    };

    // WARNING: The order and values of this enum must be kept in sync with the CommandImplementation array
    // in src/debugger/evalstackmachine.cpp. Changing this order will break the stack machine execution.
    internal enum eOpCode
    {
        IdentifierName,
        GenericName,
        InvocationExpression,
        ObjectCreationExpression,
        ElementAccessExpression,
        ElementBindingExpression,
        NumericLiteralExpression,
        StringLiteralExpression,
        CharacterLiteralExpression,
        PredefinedType,
        QualifiedName,
        AliasQualifiedName,
        MemberBindingExpression,
        ConditionalExpression,
        SimpleMemberAccessExpression,
        PointerMemberAccessExpression,
        CastExpression,
        AsExpression,
        AddExpression,
        MultiplyExpression,
        SubtractExpression,
        DivideExpression,
        ModuloExpression,
        LeftShiftExpression,
        RightShiftExpression,
        BitwiseAndExpression,
        BitwiseOrExpression,
        ExclusiveOrExpression,
        LogicalAndExpression,
        LogicalOrExpression,
        EqualsExpression,
        NotEqualsExpression,
        GreaterThanExpression,
        LessThanExpression,
        GreaterThanOrEqualExpression,
        LessThanOrEqualExpression,
        IsExpression,
        UnaryPlusExpression,
        UnaryMinusExpression,
        LogicalNotExpression,
        BitwiseNotExpression,
        TrueLiteralExpression,
        FalseLiteralExpression,
        NullLiteralExpression,
        PreIncrementExpression,
        PostIncrementExpression,
        PreDecrementExpression,
        PostDecrementExpression,
        SizeOfExpression,
        TypeOfExpression,
        CoalesceExpression,
        ThisExpression
    }

    // WARNING: This dictionary maps Roslyn SyntaxKind to eOpCode. All syntax kinds used in TreeWalker.Visit
    // must be present and map to the correct eOpCode value. Keep in sync with eOpCode enum and the
    // CommandImplementation array in src/debugger/evalstackmachine.cpp.
    static readonly Dictionary<SyntaxKind, eOpCode> KindAlias = new Dictionary<SyntaxKind, eOpCode>
    {
            { SyntaxKind.IdentifierName,                eOpCode.IdentifierName },
            { SyntaxKind.GenericName,                   eOpCode.GenericName },
            { SyntaxKind.InvocationExpression,          eOpCode.InvocationExpression },
            { SyntaxKind.ObjectCreationExpression,      eOpCode.ObjectCreationExpression },
            { SyntaxKind.ElementAccessExpression,       eOpCode.ElementAccessExpression },
            { SyntaxKind.ElementBindingExpression,      eOpCode.ElementBindingExpression },
            { SyntaxKind.NumericLiteralExpression,      eOpCode.NumericLiteralExpression },
            { SyntaxKind.StringLiteralExpression,       eOpCode.StringLiteralExpression },
            { SyntaxKind.CharacterLiteralExpression,    eOpCode.CharacterLiteralExpression },
            { SyntaxKind.PredefinedType,                eOpCode.PredefinedType },
            { SyntaxKind.QualifiedName,                 eOpCode.QualifiedName },
            { SyntaxKind.AliasQualifiedName,            eOpCode.AliasQualifiedName },
            { SyntaxKind.MemberBindingExpression,       eOpCode.MemberBindingExpression },
            { SyntaxKind.ConditionalExpression,         eOpCode.ConditionalExpression },
            { SyntaxKind.SimpleMemberAccessExpression,  eOpCode.SimpleMemberAccessExpression },
            { SyntaxKind.PointerMemberAccessExpression, eOpCode.PointerMemberAccessExpression },
            { SyntaxKind.CastExpression,                eOpCode.CastExpression },
            { SyntaxKind.AsExpression,                  eOpCode.AsExpression },
            { SyntaxKind.AddExpression,                 eOpCode.AddExpression },
            { SyntaxKind.MultiplyExpression,            eOpCode.MultiplyExpression },
            { SyntaxKind.SubtractExpression,            eOpCode.SubtractExpression },
            { SyntaxKind.DivideExpression,              eOpCode.DivideExpression },
            { SyntaxKind.ModuloExpression,              eOpCode.ModuloExpression },
            { SyntaxKind.LeftShiftExpression,           eOpCode.LeftShiftExpression },
            { SyntaxKind.RightShiftExpression,          eOpCode.RightShiftExpression },
            { SyntaxKind.BitwiseAndExpression,          eOpCode.BitwiseAndExpression },
            { SyntaxKind.BitwiseOrExpression,           eOpCode.BitwiseOrExpression },
            { SyntaxKind.ExclusiveOrExpression,         eOpCode.ExclusiveOrExpression },
            { SyntaxKind.LogicalAndExpression,          eOpCode.LogicalAndExpression },
            { SyntaxKind.LogicalOrExpression,           eOpCode.LogicalOrExpression },
            { SyntaxKind.EqualsExpression,              eOpCode.EqualsExpression },
            { SyntaxKind.NotEqualsExpression,           eOpCode.NotEqualsExpression },
            { SyntaxKind.GreaterThanExpression,         eOpCode.GreaterThanExpression },
            { SyntaxKind.LessThanExpression,            eOpCode.LessThanExpression },
            { SyntaxKind.GreaterThanOrEqualExpression,  eOpCode.GreaterThanOrEqualExpression },
            { SyntaxKind.LessThanOrEqualExpression,     eOpCode.LessThanOrEqualExpression },
            { SyntaxKind.IsExpression,                  eOpCode.IsExpression },
            { SyntaxKind.UnaryPlusExpression,           eOpCode.UnaryPlusExpression },
            { SyntaxKind.UnaryMinusExpression,          eOpCode.UnaryMinusExpression },
            { SyntaxKind.LogicalNotExpression,          eOpCode.LogicalNotExpression },
            { SyntaxKind.BitwiseNotExpression,          eOpCode.BitwiseNotExpression },
            { SyntaxKind.TrueLiteralExpression,         eOpCode.TrueLiteralExpression },
            { SyntaxKind.FalseLiteralExpression,        eOpCode.FalseLiteralExpression },
            { SyntaxKind.NullLiteralExpression,         eOpCode.NullLiteralExpression },
            { SyntaxKind.PreIncrementExpression,        eOpCode.PreIncrementExpression },
            { SyntaxKind.PostIncrementExpression,       eOpCode.PostIncrementExpression },
            { SyntaxKind.PreDecrementExpression,        eOpCode.PreDecrementExpression },
            { SyntaxKind.PostDecrementExpression,       eOpCode.PostDecrementExpression },
            { SyntaxKind.SizeOfExpression,              eOpCode.SizeOfExpression },
            { SyntaxKind.TypeOfExpression,              eOpCode.TypeOfExpression },
            { SyntaxKind.CoalesceExpression,            eOpCode.CoalesceExpression },
            { SyntaxKind.ThisExpression,                eOpCode.ThisExpression }
        };

    internal const int S_OK = 0;
    internal const int E_INVALIDARG = unchecked((int)0x80070057);

    public abstract class ICommand : IDisposable
    {
        internal eOpCode OpCode { get; private protected set; }
        protected uint Flags;
        protected IntPtr argsStructPtr;
        protected bool _disposed = false;

        public abstract IntPtr GetStructPtr();

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (argsStructPtr != IntPtr.Zero)
                {
                    Marshal.FreeCoTaskMem(argsStructPtr);
                    argsStructPtr = IntPtr.Zero;
                }
                _disposed = true;
            }
        }

        ~ICommand()
        {
            Dispose(false);
        }
    }

    public class NoOperandsCommand : ICommand
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct FormatF
        {
            public uint Flags;
        }

        public NoOperandsCommand(SyntaxKind kind, uint flags)
        {
            OpCode = KindAlias[kind];
            Flags = flags;
            argsStructPtr = IntPtr.Zero;
        }

        public override IntPtr GetStructPtr()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(NoOperandsCommand));

            if (argsStructPtr != IntPtr.Zero)
                return argsStructPtr;

            var argsStruct = new FormatF
            {
                Flags = Flags
            };
            argsStructPtr = Marshal.AllocCoTaskMem(Marshal.SizeOf<FormatF>());
            Marshal.StructureToPtr(argsStruct, argsStructPtr, false);
            return argsStructPtr;
        }

        public override string ToString()
        {
            return $"{OpCode}    flags={Flags}";
        }
    }

    public class OneOperandCommand : ICommand
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct FormatFS
        {
            public uint Flags;
            public IntPtr String;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct FormatFI
        {
            public uint Flags;
            public int Int;
        }

        private readonly object Argument;
        private bool stringAllocated = false;

        public OneOperandCommand(SyntaxKind kind, uint flags, object arg)
        {
            OpCode = KindAlias[kind];
            Flags = flags;
            Argument = arg;
            argsStructPtr = IntPtr.Zero;
        }

        protected override void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (argsStructPtr != IntPtr.Zero && stringAllocated)
                {
                    // The argsStructPtr points to a FormatFS struct (when stringAllocated is true)
                    // We need to extract the BSTR from the struct's String field and free it
                    var fs = Marshal.PtrToStructure<FormatFS>(argsStructPtr);
                    if (fs.String != IntPtr.Zero)
                    {
                        Marshal.FreeBSTR(fs.String);
                    }
                }
                // Let base class free argsStructPtr itself
                base.Dispose(disposing);
            }
        }

        public override IntPtr GetStructPtr()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(OneOperandCommand));

            if (argsStructPtr != IntPtr.Zero)
                return argsStructPtr;

            if (Argument is string || Argument is char)
            {
                var fs = new FormatFS
                {
                    Flags = Flags,
                    String = Marshal.StringToBSTR(Argument.ToString())
                };
                argsStructPtr = Marshal.AllocCoTaskMem(Marshal.SizeOf<FormatFS>());
                Marshal.StructureToPtr(fs, argsStructPtr, false);
                stringAllocated = true;
            }
            else if (Argument is int || Argument is ePredefinedType)
            {
                var fi = new FormatFI
                {
                    Flags = Flags,
                    Int = (int)Argument
                };
                argsStructPtr = Marshal.AllocCoTaskMem(Marshal.SizeOf<FormatFI>());
                Marshal.StructureToPtr(fi, argsStructPtr, false);
            }
            else
            {
                throw new NotImplementedException($"{Argument.GetType()} type not implemented in {nameof(OneOperandCommand)}!");
            }

            return argsStructPtr;
        }

        public override string ToString()
        {
            return $"{OpCode}    flags={Flags}    {Argument}";
        }
    }

    public class TwoOperandCommand : ICommand
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct FormatFIS
        {
            public uint Flags;
            public int Int;
            public IntPtr String;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct FormatFIP
        {
            public uint Flags;
            public int Int;
            public IntPtr Ptr;
        }

        // WARNING: The following structs (FormatF, FormatFS, FormatFI, FormatFIS, FormatFIP) must match the layout
        // of the corresponding C++ structs in src/debugger/evalstackmachine.cpp exactly. Do not change field types, order, or names.
        private readonly object[] Arguments;
        private bool stringAllocated = false;
        private bool ptrAllocated = false;

        public TwoOperandCommand(SyntaxKind kind, uint flags, params object[] args)
        {
            OpCode = KindAlias[kind];
            Flags = flags;
            Arguments = args;
            argsStructPtr = IntPtr.Zero;
        }

        protected override void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (argsStructPtr != IntPtr.Zero)
                {
                    if (stringAllocated)
                    {
                        var fs = Marshal.PtrToStructure<FormatFIS>(argsStructPtr);
                        if (fs.String != IntPtr.Zero)
                        {
                            Marshal.FreeBSTR(fs.String);
                        }
                    }
                    else if (ptrAllocated)
                    {
                        var fp = Marshal.PtrToStructure<FormatFIP>(argsStructPtr);
                        if (fp.Ptr != IntPtr.Zero)
                        {
                            Marshal.FreeCoTaskMem(fp.Ptr);
                        }
                    }
                }
                // Let base class free argsStructPtr itself
                base.Dispose(disposing);
            }
        }

        public override IntPtr GetStructPtr()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(TwoOperandCommand));

            if (argsStructPtr != IntPtr.Zero)
                return argsStructPtr;

            if (Arguments[0] is string && Arguments[1] is int)
            {
                var fis = new FormatFIS
                {
                    Flags = Flags,
                    String = Marshal.StringToBSTR(Arguments[0].ToString()),
                    Int = (int)Arguments[1]
                };
                argsStructPtr = Marshal.AllocCoTaskMem(Marshal.SizeOf<FormatFIS>());
                Marshal.StructureToPtr(fis, argsStructPtr, false);
                stringAllocated = true;
            }
            else if (Arguments[0] is ePredefinedType)
            {
                int size = 0;
                IntPtr data = IntPtr.Zero;
                MarshalValue(Arguments[1], out size, out data);
                var fip = new FormatFIP
                {
                    Flags = Flags,
                    Int = (int)Arguments[0],
                    Ptr = data
                };
                argsStructPtr = Marshal.AllocCoTaskMem(Marshal.SizeOf<FormatFIP>());
                Marshal.StructureToPtr(fip, argsStructPtr, false);
                ptrAllocated = true;
            }
            else
            {
                throw new NotImplementedException($"{Arguments[0].GetType()} + {Arguments[1].GetType()} pair not implemented in {nameof(TwoOperandCommand)}!");
            }

            return argsStructPtr;
        }

        public override string ToString()
        {
            var sb = new StringBuilder();
            sb.Append($"{OpCode}    flags={Flags}");
            foreach (var arg in Arguments)
            {
                sb.Append($"    {arg}");
            }
            return sb.ToString();
        }
    }

    // WARNING: StackMachineProgram must match C++ expectations in src/debugger/evalstackmachine.cpp.
    // This class is passed across the managed/unmanaged boundary.
    public class StackMachineProgram : IDisposable
    {
        public static readonly int ProgramInProgress = 0;
        public static readonly int ProgramFinished = -1;
        public static readonly int BeforeFirstCommand = -2;
        public int CurrentPosition { get; set; } = BeforeFirstCommand;
        public List<ICommand> Commands { get; } = new List<ICommand>();
        private bool _disposed = false;

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    foreach (var cmd in Commands)
                    {
                        cmd?.Dispose();
                    }
                    Commands.Clear();
                }
                _disposed = true;
            }
        }

        ~StackMachineProgram()
        {
            Dispose(false);
        }
    }

    public class SyntaxKindNotImplementedException : NotImplementedException
    {
        public SyntaxKindNotImplementedException()
        {
        }

        public SyntaxKindNotImplementedException(string message)
            : base(message)
        {
        }

        public SyntaxKindNotImplementedException(string message, Exception inner)
            : base(message, inner)
        {
        }
    }

    public class TreeWalker : CSharpSyntaxWalker
    {
        private bool isInExpressionStatement = false;
        public int ExpressionStatementCount { get; private set; } = 0;
#if DEBUG_STACKMACHINE
        // Gather AST data for DebugText.
        private List<string> ST = new List<string>();
        private int CurrentNodeDepth = 0;
#endif

        // CheckedExpression/UncheckedExpression syntax kind related
        private static readonly uint maskChecked = 0xFFFFFFFE;
        private static readonly uint flagChecked = 0x00000001;
        private static readonly uint flagUnchecked = 0x00000000;
        // Tracking current AST scope flags.
        private static readonly uint defaultScopeFlags = flagUnchecked;
        private Stack<uint> CurrentScopeFlags = new Stack<uint>();

        internal StackMachineProgram stackMachineProgram = new StackMachineProgram();

        public override void Visit(SyntaxNode node)
        {
            if (Microsoft.CodeAnalysis.CSharpExtensions.IsKind(node, SyntaxKind.ExpressionStatement))
            {
                ExpressionStatementCount++;
                isInExpressionStatement = true;
                base.Visit(node);
                isInExpressionStatement = false;
            }
            else if (isInExpressionStatement)
            {
                // Setup flags before base.Visit() call, since all nested Kinds must have proper flags.
                if (CurrentScopeFlags.Count == 0)
                {
                    CurrentScopeFlags.Push(defaultScopeFlags);
                }
                else
                {
                    CurrentScopeFlags.Push(CurrentScopeFlags.Peek());
                }

                switch (node.Kind())
                {
                case SyntaxKind.UncheckedExpression:
                    CurrentScopeFlags.Push((CurrentScopeFlags.Pop() & maskChecked) | flagUnchecked);
                    break;
                case SyntaxKind.CheckedExpression:
                    CurrentScopeFlags.Push((CurrentScopeFlags.Pop() & maskChecked) | flagChecked);
                    break;
                }
#if DEBUG_STACKMACHINE
                CurrentNodeDepth++;

                // Gather AST data for DebugText.
                var indents = new String(' ', CurrentNodeDepth * 4);
                ST.Add(indents + node.Kind() + " --- " + node.ToString());
#endif
                // Visit nested Kinds in proper order.
                // Note, we should setup flags before and parse Kinds after this call.
                base.Visit(node);
#if DEBUG_STACKMACHINE
                CurrentNodeDepth--;
#endif
                switch (node.Kind())
                {
                    /*
                    DefaultExpression - should not be in expression AST
                    */

                case SyntaxKind.IdentifierName:
                case SyntaxKind.StringLiteralExpression:
                    stackMachineProgram.Commands.Add(new OneOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), node.GetFirstToken().Value));
                    break;

                case SyntaxKind.GenericName:
                    // GenericName
                    //     \ TypeArgumentList
                    //           \ types OR OmittedTypeArgument
                    int? GenericNameArgs = null;
                    bool OmittedTypeArg = false;
                    foreach (var child in node.ChildNodes())
                    {
                        if (!Microsoft.CodeAnalysis.CSharpExtensions.IsKind(child, SyntaxKind.TypeArgumentList))
                            continue;

                        GenericNameArgs = 0;

                        foreach (var ArgumentListChild in child.ChildNodes())
                        {
                            if (Microsoft.CodeAnalysis.CSharpExtensions.IsKind(ArgumentListChild, SyntaxKind.OmittedTypeArgument))
                            {
                                OmittedTypeArg = true;
                                break;
                            }

                            GenericNameArgs++;
                        }
                    }
                    if (!GenericNameArgs.HasValue || (GenericNameArgs.Value < 1 && !OmittedTypeArg))
                    {
                        throw new ArgumentOutOfRangeException(nameof(node), $"{node.Kind()} must have at least one type!");
                    }
                    stackMachineProgram.Commands.Add(new TwoOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), node.GetFirstToken().Value, GenericNameArgs));
                    break;

                case SyntaxKind.InvocationExpression:
//TODO          case SyntaxKind.ObjectCreationExpression:
                    // InvocationExpression/ObjectCreationExpression
                    //     \ ArgumentList
                    //           \ Argument
                    int? ArgsCount = null;
                    foreach (var child in node.ChildNodes())
                    {
                        if (!Microsoft.CodeAnalysis.CSharpExtensions.IsKind(child, SyntaxKind.ArgumentList))
                            continue;

                        ArgsCount = 0;

                        foreach (var ArgumentListChild in child.ChildNodes())
                        {
                            if (!Microsoft.CodeAnalysis.CSharpExtensions.IsKind(ArgumentListChild, SyntaxKind.Argument))
                                continue;

                            ArgsCount++;
                        }
                    }
                    if (!ArgsCount.HasValue)
                    {
                        throw new ArgumentOutOfRangeException(nameof(node), $"{node.Kind()} must have at least one argument!");
                    }
                    stackMachineProgram.Commands.Add(new OneOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), ArgsCount));
                    break;

                case SyntaxKind.ElementAccessExpression:
                case SyntaxKind.ElementBindingExpression:
                    // ElementAccessExpression/ElementBindingExpression
                    //     \ BracketedArgumentList
                    //           \ Argument
                    int? ElementAccessArgs = null;
                    foreach (var child in node.ChildNodes())
                    {
                        if (!Microsoft.CodeAnalysis.CSharpExtensions.IsKind(child, SyntaxKind.BracketedArgumentList))
                            continue;

                        ElementAccessArgs = 0;

                        foreach (var ArgumentListChild in child.ChildNodes())
                        {
                            if (!Microsoft.CodeAnalysis.CSharpExtensions.IsKind(ArgumentListChild, SyntaxKind.Argument))
                                continue;

                            ElementAccessArgs++;
                        }
                    }
                    if (!ElementAccessArgs.HasValue)
                    {
                        throw new ArgumentOutOfRangeException(nameof(node), $"{node.Kind()} must have at least one argument!");
                    }
                    stackMachineProgram.Commands.Add(new OneOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), ElementAccessArgs));
                    break;

                case SyntaxKind.NumericLiteralExpression:
                case SyntaxKind.CharacterLiteralExpression: // 1 wchar
                    stackMachineProgram.Commands.Add(new TwoOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), TypeAlias[node.GetFirstToken().Value.GetType()], node.GetFirstToken().Value));
                    break;

                case SyntaxKind.PredefinedType:
                    stackMachineProgram.Commands.Add(new OneOperandCommand(node.Kind(), CurrentScopeFlags.Peek(), TypeKindAlias[node.GetFirstToken().Kind()]));
                    break;

                // skip, in case of stack machine program creation we don't use this kinds directly
                case SyntaxKind.Argument:
                case SyntaxKind.BracketedArgumentList:
                case SyntaxKind.ConditionalAccessExpression:
                case SyntaxKind.ArgumentList:
                case SyntaxKind.ParenthesizedExpression:
                case SyntaxKind.TypeArgumentList:
/* TODO
                case SyntaxKind.OmittedTypeArgument:
                case SyntaxKind.UncheckedExpression:
                case SyntaxKind.CheckedExpression:
*/
                    break;

                case SyntaxKind.SimpleMemberAccessExpression:
                case SyntaxKind.TrueLiteralExpression:
                case SyntaxKind.FalseLiteralExpression:
                case SyntaxKind.NullLiteralExpression:
                case SyntaxKind.ThisExpression:
                case SyntaxKind.MemberBindingExpression:
                case SyntaxKind.UnaryPlusExpression:
                case SyntaxKind.UnaryMinusExpression:
                case SyntaxKind.AddExpression:
                case SyntaxKind.MultiplyExpression:
                case SyntaxKind.SubtractExpression:
                case SyntaxKind.DivideExpression:
                case SyntaxKind.ModuloExpression:
                case SyntaxKind.RightShiftExpression:
                case SyntaxKind.LeftShiftExpression:
                case SyntaxKind.BitwiseNotExpression:
                case SyntaxKind.LogicalAndExpression:
                case SyntaxKind.LogicalOrExpression:
                case SyntaxKind.ExclusiveOrExpression:
                case SyntaxKind.BitwiseAndExpression:
                case SyntaxKind.BitwiseOrExpression:
                case SyntaxKind.LogicalNotExpression:
                case SyntaxKind.EqualsExpression:
                case SyntaxKind.NotEqualsExpression:
                case SyntaxKind.GreaterThanExpression:
                case SyntaxKind.LessThanExpression:
                case SyntaxKind.GreaterThanOrEqualExpression:
                case SyntaxKind.LessThanOrEqualExpression:
                case SyntaxKind.QualifiedName:
                case SyntaxKind.CoalesceExpression:

/* TODO
                case SyntaxKind.AliasQualifiedName:
                case SyntaxKind.ConditionalExpression:
                case SyntaxKind.PointerMemberAccessExpression:
                case SyntaxKind.CastExpression:
                case SyntaxKind.AsExpression:
                case SyntaxKind.IsExpression:
                case SyntaxKind.PreIncrementExpression:
                case SyntaxKind.PostIncrementExpression:
                case SyntaxKind.PreDecrementExpression:
                case SyntaxKind.PostDecrementExpression:
*/
                case SyntaxKind.SizeOfExpression:
/* TODO
                case SyntaxKind.TypeOfExpression:
*/
                    stackMachineProgram.Commands.Add(new NoOperandsCommand(node.Kind(), CurrentScopeFlags.Peek()));
                    break;

                default:
                    throw new SyntaxKindNotImplementedException($"{node.Kind()} not implemented!");
                }

                CurrentScopeFlags.Pop();
            }
            else
            {
                // skip CompilationUnit, GlobalStatement and ExpressionStatement kinds
                base.Visit(node);
            }
        }

#if DEBUG_STACKMACHINE
        public string GenerateDebugText()
        {
            // We cannot derive from sealed type 'StringBuilder' and it use platform-dependant Environment.NewLine for new line.
            // Use '\n' directly, since dncdbg use only '\n' for new line.
            var lines = new List<string>();
            lines.Add("=======================================");
            lines.Add("Source tree:");
            lines.AddRange(ST);
            lines.Add("=======================================");
            lines.Add("Stack machine commands:");
            foreach (var command in stackMachineProgram.Commands)
            {
                lines.Add($"    {command.ToString()}");
            }
            return string.Join("\n", lines);
        }
#endif
    }

    /// <summary>
    /// Generate stack machine program by expression string.
    /// </summary>
    /// <param name="expression">expression string</param>
    /// <param name="stackProgram">stack machine program handle return</param>
    /// <param name="textOutput">BSTR with text information return</param>
    /// <returns>HResult code with execution status</returns>
    internal static int GenerateStackMachineProgram([MarshalAs(UnmanagedType.LPWStr)] string expression,
                                                    out IntPtr stackProgram, out IntPtr textOutput)
    {
        stackProgram = IntPtr.Zero;
        textOutput = IntPtr.Zero;

        try
        {
            var parseOptions = CSharpParseOptions.Default.WithKind(SourceCodeKind.Script); // in order to parse individual expression
            var tree = CSharpSyntaxTree.ParseText(expression, parseOptions);

            var parseErrors = tree.GetDiagnostics(tree.GetRoot());
            var errorMessages = new List<string>();
            bool errorDetected = false;
            foreach (var error in parseErrors)
            {
                errorMessages.Add($"error {error.Id}: {error.GetMessage()}");
                errorDetected = true;
            }

            if (errorDetected)
            {
                textOutput = Marshal.StringToBSTR(string.Join("\n", errorMessages));
                return E_INVALIDARG;
            }

            var treeWalker = new TreeWalker();
            treeWalker.Visit(tree.GetRoot());

            if (treeWalker.ExpressionStatementCount == 1)
            {
#if DEBUG_STACKMACHINE
                textOutput = Marshal.StringToBSTR(treeWalker.GenerateDebugText());
#endif
                GCHandle gch = GCHandle.Alloc(treeWalker.stackMachineProgram);
                stackProgram = GCHandle.ToIntPtr(gch);
                return S_OK;
            }
            else if (treeWalker.ExpressionStatementCount > 1)
            {
                textOutput = Marshal.StringToBSTR($"error: only one expression must be provided, expressions found: {treeWalker.ExpressionStatementCount}");
                return E_INVALIDARG;
            }
            else
            {
                textOutput = Marshal.StringToBSTR("error: no expression found");
                return E_INVALIDARG;
            }
        }
        catch (Exception e)
        {
            textOutput = Marshal.StringToBSTR($"{e.GetType()}: {e.Message}");
            return e.HResult;
        }
    }

    /// <summary>
    /// Release previously allocated memory.
    /// </summary>
    /// <param name="StackProgram">stack machine program handle returned by GenerateStackMachineProgram()</param>
    /// <returns></returns>
    internal static void ReleaseStackMachineProgram(IntPtr StackProgram)
    {
        Debug.Assert(StackProgram != IntPtr.Zero);
        try
        {
            GCHandle gch = GCHandle.FromIntPtr(StackProgram);
            if (gch.Target is StackMachineProgram program)
            {
                program.Dispose();
            }
            gch.Free();
        }
        catch
        {
            // suppress any exceptions and continue execution
        }
    }

    /// <summary>
    /// Return next stack program command and pointer to argument's structure.
    /// Note, managed part will release Arguments unmanaged memory at object finalizer call after ReleaseStackMachineProgram() call.
    /// Native part must not release Arguments memory, allocated by managed part in this method.
    /// </summary>
    /// <param name="StackProgram">stack machine program handle returned by GenerateStackMachineProgram()</param>
    /// <param name="Command">next stack machine program command return</param>
    /// <param name="Arguments">pointer to Arguments unmanaged memory return</param>
    /// <param name="textOutput">BSTR with text information return</param>
    /// <returns>HResult code with execution status</returns>
    internal static int NextStackCommand(IntPtr StackProgram, out int Command, out IntPtr Arguments, out IntPtr textOutput)
    {
        Debug.Assert(StackProgram != IntPtr.Zero);

        Command = StackMachineProgram.ProgramInProgress;
        Arguments = IntPtr.Zero;
        textOutput = IntPtr.Zero;

        try
        {
            GCHandle gch = GCHandle.FromIntPtr(StackProgram);
            StackMachineProgram stackProgram = (StackMachineProgram)gch.Target;

            if (stackProgram.CurrentPosition == StackMachineProgram.BeforeFirstCommand)
            {
                stackProgram.CurrentPosition = 0;
            }
            else
            {
                stackProgram.CurrentPosition++;
            }

            if (stackProgram.CurrentPosition >= stackProgram.Commands.Count)
            {
                Command = StackMachineProgram.ProgramFinished;
            }
            else
            {
                // Note, enum must be explicitly converted to int.
                Command = (int)stackProgram.Commands[stackProgram.CurrentPosition].OpCode;
                Arguments = stackProgram.Commands[stackProgram.CurrentPosition].GetStructPtr();
            }

            return S_OK;
        }
        catch (Exception e)
        {
            textOutput = Marshal.StringToBSTR($"{e.GetType()}: {e.Message}");
            return e.HResult;
        }
    }

    #endregion // Type Definitions
}
}
