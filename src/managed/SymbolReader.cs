// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Runtime.InteropServices;

namespace DNCDbg
{
/// <summary>
/// Provides symbol reading capabilities for PDB files and debug information.
/// This class handles reading sequence points, local variables, and method information
/// from portable PDB files for use by the debugger.
/// </summary>
public class SymbolReader
{
    #region Resource Management

    /// <summary>
    /// Helper class to safely manage allocated resources for interop.
    /// Tracks both CoTaskMem allocations and BSTR allocations for proper cleanup.
    /// </summary>
    private class ResourceTracker : IDisposable
    {
        private readonly List<IntPtr> m_allocatedMemory = new List<IntPtr>();
        private readonly List<IntPtr> m_allocatedBSTRs = new List<IntPtr>();
        private bool m_disposed = false;
        private bool m_transferOwnership = false;

        public IntPtr TrackMemory(IntPtr memory)
        {
            if (memory != IntPtr.Zero)
                m_allocatedMemory.Add(memory);
            return memory;
        }

        public IntPtr TrackBSTR(IntPtr bstr)
        {
            if (bstr != IntPtr.Zero)
                m_allocatedBSTRs.Add(bstr);
            return bstr;
        }

        public void TransferOwnership()
        {
            m_transferOwnership = true;
        }

        public void Dispose()
        {
            if (!m_disposed)
            {
                if (!m_transferOwnership)
                {
                    // Dispose in reverse order of allocation
                    for (int i = m_allocatedMemory.Count - 1; i >= 0; i--)
                    {
                        if (m_allocatedMemory[i] != IntPtr.Zero)
                            Marshal.FreeCoTaskMem(m_allocatedMemory[i]);
                    }

                    for (int i = m_allocatedBSTRs.Count - 1; i >= 0; i--)
                    {
                        if (m_allocatedBSTRs[i] != IntPtr.Zero)
                            Marshal.FreeBSTR(m_allocatedBSTRs[i]);
                    }
                }
                m_allocatedMemory.Clear();
                m_allocatedBSTRs.Clear();
                m_disposed = true;
            }
        }
    }

    /// <summary>
    /// Helper method to safely get reader from symbol reader handle
    /// </summary>
    private static bool TryGetReader(IntPtr symbolReaderHandle, out MetadataReader reader)
    {
        reader = null;
        try
        {
            GCHandle gch = GCHandle.FromIntPtr(symbolReaderHandle);
            reader = ((OpenedReader)gch.Target).Reader;
            return true;
        }
        catch
        {
            return false;
        }
    }

    /// <summary>
    /// Helper method to safely get reader from symbol reader handle with validation
    /// </summary>
    private static bool TryGetReaderWithValidation(IntPtr symbolReaderHandle, out MetadataReader reader)
    {
        reader = null;
        if (symbolReaderHandle == IntPtr.Zero)
            return false;

        return TryGetReader(symbolReaderHandle, out reader);
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct SequencePoint)
    // Note: C++ side uses SequencePoint, C# uses DbgSequencePoint to avoid confusion with MetadataReader.SequencePoint.
    [StructLayout(LayoutKind.Sequential)]
    internal struct DbgSequencePoint
    {
        public int startLine;
        public int startColumn;
        public int endLine;
        public int endColumn;
        public int offset;
        public IntPtr document;
    }

    /// <summary>
    /// Read memory callback
    /// </summary>
    /// <returns>number of bytes read or 0 for error</returns>
    internal unsafe delegate int ReadMemoryDelegate(IntPtr address, byte *buffer, int count);

    private sealed class OpenedReader : IDisposable
    {
        public readonly MetadataReaderProvider Provider;
        public readonly MetadataReader Reader;
        public readonly string PDBPath;

        public OpenedReader(MetadataReaderProvider provider, MetadataReader reader, string pdbPath)
        {
            Debug.Assert(provider != null);
            Debug.Assert(reader != null);

            Provider = provider;
            Reader = reader;
            PDBPath = pdbPath;
        }

        public void Dispose() => Provider.Dispose();
    }

    /// <summary>
    /// Stream implementation to read debugger target memory for in-memory PDBs
    /// </summary>
    private sealed class TargetStream : Stream
    {
        private readonly IntPtr _address;
        private readonly ReadMemoryDelegate _readMemory;

        public override long Position { get; set; }
        public override long Length { get; }
        public override bool CanSeek => true;
        public override bool CanRead => true;
        public override bool CanWrite => false;

        public TargetStream(IntPtr address, int size, ReadMemoryDelegate readMemory)
        {
            _address = address;
            _readMemory = readMemory ?? throw new ArgumentNullException(nameof(readMemory));
            Length = size;
            Position = 0;
        }

        public override int Read(byte[] buffer, int offset, int count)
        {
            if (buffer is null)
                throw new ArgumentNullException(nameof(buffer));
            if (offset < 0)
                throw new ArgumentOutOfRangeException(nameof(offset));
            if (count < 0)
                throw new ArgumentOutOfRangeException(nameof(count));
            if (offset + count > buffer.Length)
                throw new ArgumentException("Offset plus count exceeds buffer length", nameof(offset));

            if (Position + count > Length)
                throw new ArgumentOutOfRangeException(nameof(count));

            unsafe
            {
                fixed (byte* p = &buffer[offset])
                {
                    int read = _readMemory(IntPtr.Add(_address, (int)Position), p, count);
                    Position += read;
                    return read;
                }
            }
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            switch (origin)
            {
            case SeekOrigin.Begin:
                Position = offset;
                break;
            case SeekOrigin.End:
                Position = Length + offset;
                break;
            case SeekOrigin.Current:
                Position += offset;
                break;
            }
            return Position;
        }

        public override void Flush()
        {
        }

        public override void SetLength(long value)
        {
            throw new NotSupportedException();
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            throw new NotSupportedException();
        }
    }

    /// <summary>
    /// Quick fix for Path.GetFileName which incorrectly handles Windows-style paths on Linux
    /// </summary>
    /// <param name="pathName">File path to be processed</param>
    /// <returns>Last component of path</returns>
    private static string GetFileName(string pathName)
    {
        if (pathName is null)
            throw new ArgumentNullException(nameof(pathName));

        int pos = pathName.LastIndexOfAny(new[] { '/', '\\' });
        return pos < 0 ? pathName : pathName.Substring(pos + 1);
    }

    /// <summary>
    /// Checks availability of debugging information for given assembly.
    /// </summary>
    /// <param name="assemblyPath">
    /// File path of the assembly or null if the module is in-memory or dynamic (generated by Reflection.Emit)
    /// </param>
    /// <param name="isFileLayout">type of in-memory PE layout, if true, file based layout otherwise, loaded layout</param>
    /// <param name="loadedPeAddress">
    /// Loaded PE image address or zero if the module is dynamic (generated by Reflection.Emit).
    /// Dynamic modules have their PDBs (if any) generated to an in-memory stream
    /// (pointed to by <paramref name="inMemoryPdbAddress"/> and <paramref name="inMemoryPdbSize"/>).
    /// </param>
    /// <param name="loadedPeSize">loaded PE image size</param>
    /// <param name="inMemoryPdbAddress">in memory PDB address or zero</param>
    /// <param name="inMemoryPdbSize">in memory PDB size</param>
    /// <param name="readMemory">read memory callback</param>
    /// <param name="pdbName">pdb file path</param>
    /// <returns>Symbol reader handle or zero if error</returns>
    internal static IntPtr LoadSymbolsForModule([MarshalAs(UnmanagedType.LPWStr)] string assemblyPath,
                                                bool isFileLayout, IntPtr loadedPeAddress, int loadedPeSize,
                                                IntPtr inMemoryPdbAddress, int inMemoryPdbSize,
                                                ReadMemoryDelegate readMemory, out IntPtr pdbName)
    {
        GCHandle gch = new GCHandle();
        TargetStream peStream = null;
        TargetStream pdbStream = null;
        pdbName = IntPtr.Zero;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (assemblyPath is null && loadedPeAddress != IntPtr.Zero)
                {
                    peStream = new TargetStream(loadedPeAddress, loadedPeSize, readMemory);
                }
                if (inMemoryPdbAddress != IntPtr.Zero)
                {
                    pdbStream = new TargetStream(inMemoryPdbAddress, inMemoryPdbSize, readMemory);
                }
                OpenedReader openedReader = GetReader(assemblyPath, isFileLayout, peStream, pdbStream);
                if (openedReader != null)
                {
                    if (!string.IsNullOrEmpty(openedReader.PDBPath))
                    {
                        IntPtr allocatedBSTR = resourceTracker.TrackBSTR(Marshal.StringToBSTR(openedReader.PDBPath));
                        pdbName = allocatedBSTR;

                        resourceTracker.TransferOwnership();
                    }
                    gch = GCHandle.Alloc(openedReader);
                    return GCHandle.ToIntPtr(gch);
                }
            }
            catch
            {
                // If we allocated a handle, free it to prevent a memory leak
                if (gch.IsAllocated)
                {
                    try
                    {
                        ((OpenedReader)gch.Target).Dispose();
                        gch.Free();
                    }
                    catch
                    {
                        // Ignore exceptions during cleanup
                    }
                }

                // Dispose streams if they were created but not transferred to the reader
                peStream?.Dispose();
                pdbStream?.Dispose();
            }
        }

        return IntPtr.Zero;
    }

    /// <summary>
    /// Maps global method token to a handle local to the current delta PDB.
    /// Debug tables referring to methods currently use local handles, not global handles.
    /// See https://github.com/dotnet/roslyn/issues/16286
    /// </summary>
    private static MethodDefinitionHandle GetDeltaRelativeMethodDefinitionHandle(MetadataReader reader, int methodToken)
    {
        var globalHandle = (MethodDefinitionHandle)MetadataTokens.EntityHandle(methodToken);

        if (reader.GetTableRowCount(TableIndex.EncMap) == 0)
        {
            return globalHandle;
        }

        var globalDebugHandle = globalHandle.ToDebugInformationHandle();

        int rowId = 1;
        foreach (var handle in reader.GetEditAndContinueMapEntries())
        {
            if (handle.Kind == HandleKind.MethodDebugInformation)
            {
                if (handle == globalDebugHandle)
                {
                    return MetadataTokens.MethodDefinitionHandle(rowId);
                }

                rowId++;
            }
        }

        // compiler generated invalid EncMap table:
        throw new BadImageFormatException();
    }

    /// <summary>
    /// Cleanup and dispose of symbol reader handle
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    internal static void Dispose(IntPtr symbolReaderHandle)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        try
        {
            GCHandle gch = GCHandle.FromIntPtr(symbolReaderHandle);
            ((OpenedReader)gch.Target).Dispose();
            gch.Free();
        }
        catch
        {
        }
    }

    internal static SequencePointCollection GetSequencePointCollection(int methodToken, MetadataReader reader)
    {
        Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
        if (handle.Kind != HandleKind.MethodDefinition)
            return new SequencePointCollection();

        MethodDebugInformationHandle methodDebugHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
        if (methodDebugHandle.IsNil)
            return new SequencePointCollection();

        MethodDebugInformation methodDebugInfo = reader.GetMethodDebugInformation(methodDebugHandle);
        return methodDebugInfo.GetSequencePoints();
    }

    /// <summary>
    /// Find current user code sequence point by IL offset.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="ilOffset">IL offset</param>
    /// <param name="sequencePoint">sequence point return</param>
    /// <returns>"Ok" if information is available</returns>
    private static RetCode GetSequencePointByILOffset(IntPtr symbolReaderHandle, int methodToken, uint ilOffset,
                                                      out DbgSequencePoint sequencePoint)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        sequencePoint.document = IntPtr.Zero;
        sequencePoint.startLine = 0;
        sequencePoint.startColumn = 0;
        sequencePoint.endLine = 0;
        sequencePoint.endColumn = 0;
        sequencePoint.offset = 0;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                SequencePointCollection sequencePoints = GetSequencePointCollection(methodToken, reader);

                SequencePoint nearestPoint = default;
                bool found = false;

                foreach (SequencePoint point in sequencePoints)
                {
                    if (found && point.Offset > ilOffset)
                        break;

                    if (point.StartLine != 0 && point.StartLine != SequencePoint.HiddenLine)
                    {
                        nearestPoint = point;
                        found = true;
                    }
                }

                if (!found)
                    return RetCode.Fail;

                var fileName = reader.GetString(reader.GetDocument(nearestPoint.Document).Name);
                IntPtr documentBSTR = resourceTracker.TrackBSTR(Marshal.StringToBSTR(fileName));
                sequencePoint.document = documentBSTR;
                sequencePoint.startLine = nearestPoint.StartLine;
                sequencePoint.startColumn = nearestPoint.StartColumn;
                sequencePoint.endLine = nearestPoint.EndLine;
                sequencePoint.endColumn = nearestPoint.EndColumn;
                sequencePoint.offset = nearestPoint.Offset;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    /// <summary>
    /// Find IL offset for next close user code sequence point by IL offset.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="ilOffset">IL offset</param>
    /// <param name="sequencePoint">sequence point return</param>
    /// <param name="noUserCodeFound">return 1 in case all sequence points checked and no user code was found, otherwise return 0</param>
    /// <returns>"Ok" if information is available</returns>
    private static RetCode GetNextUserCodeILOffset(IntPtr symbolReaderHandle, int methodToken, uint ilOffset, out uint ilNextOffset, out int noUserCodeFound)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        ilNextOffset = 0;
        noUserCodeFound = 0;

        try
        {
            if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                return RetCode.Fail;

            SequencePointCollection sequencePoints = GetSequencePointCollection(methodToken, reader);

            foreach (SequencePoint point in sequencePoints)
            {
                if (point.StartLine == 0 || point.StartLine == SequencePoint.HiddenLine)
                    continue;

                if (point.Offset >= ilOffset)
                {
                    ilNextOffset = (uint)point.Offset;
                    return RetCode.OK;
                }
            }

            noUserCodeFound = 1;
            return RetCode.Fail;
        }
        catch
        {
            return RetCode.Exception;
        }
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct method_data_t)
    [StructLayout(LayoutKind.Sequential)]
    internal struct method_data_t
    {
        public int methodDef;
        public int startLine;   // first segment/method SequencePoint's startLine
        public int endLine;     // last segment/method SequencePoint's endLine
        public int startColumn; // first segment/method SequencePoint's startColumn
        public int endColumn;   // last segment/method SequencePoint's endColumn
        public int isCtor;      // is method data constructor related

        public method_data_t(int methodDef_, int startLine_, int endLine_, int startColumn_, int endColumn_, int isCtor_)
        {
            methodDef = methodDef_;
            startLine = startLine_;
            endLine = endLine_;
            startColumn = startColumn_;
            endColumn = endColumn_;
            isCtor = isCtor_;
        }
        public void SetRange(int startLine_, int endLine_, int startColumn_, int endColumn_)
        {
            startLine = startLine_;
            endLine = endLine_;
            startColumn = startColumn_;
            endColumn = endColumn_;
        }
        public void ExtendRange(int startLine_, int endLine_, int startColumn_, int endColumn_)
        {
            if (startLine > startLine_)
            {
                startLine = startLine_;
                startColumn = startColumn_;
            }
            else if (startLine == startLine_ && startColumn > startColumn_)
            {
                startColumn = startColumn_;
            }

            if (endLine < endLine_)
            {
                endLine = endLine_;
                endColumn = endColumn_;
            }
            else if (endLine == endLine_ && endColumn < endColumn_)
            {
                endColumn = endColumn_;
            }
        }
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct file_methods_data_t)
    [StructLayout(LayoutKind.Sequential)]
    internal struct file_methods_data_t
    {
        public IntPtr document;
        public int methodNum;
        public IntPtr methodsData; // method_data_t*
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct module_methods_data_t)
    [StructLayout(LayoutKind.Sequential)]
    internal struct module_methods_data_t
    {
        public int fileNum;
        public IntPtr moduleMethodsData; // file_methods_data_t*
    }

    /// <summary>
    /// Get all method ranges for all methods (in case of constructors ranges for all segments).
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="constrNum">number of constructors tokens in array</param>
    /// <param name="constrTokens">array of constructors tokens</param>
    /// <param name="normalNum">number of normal methods tokens in array</param>
    /// <param name="normalTokens">array of normal methods tokens</param>
    /// <param name="data">pointer to memory with result</param>
    /// <returns>"Ok" if information is available</returns>
    internal static RetCode GetModuleMethodsRanges(IntPtr symbolReaderHandle, uint constrNum, IntPtr constrTokens,
                                                   uint normalNum, IntPtr normalTokens, out IntPtr data)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        data = IntPtr.Zero;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                Dictionary<DocumentHandle, List<method_data_t>> moduleData = new Dictionary<DocumentHandle, List<method_data_t>>();

                // Make sure we add constructors related data first, since this data can't be nested for sure.
                for (int i = 0; i < constrNum; i++)
                {
                    int methodToken = Marshal.ReadInt32(constrTokens, i * 4);
                    method_data_t currentData = new method_data_t(methodToken, 0, 0, 0, 0, 1);

                    foreach (SequencePoint p in GetSequencePointCollection(methodToken, reader))
                    {
                        if (p.StartLine == 0 || p.StartLine == SequencePoint.HiddenLine)
                            continue;

                        if (!moduleData.ContainsKey(p.Document))
                            moduleData[p.Document] = new List<method_data_t>();

                        currentData.SetRange(p.StartLine, p.EndLine, p.StartColumn, p.EndColumn);
                        moduleData[p.Document].Add(currentData);
                    }
                }

                for (int i = 0; i < normalNum; i++)
                {
                    int methodToken = Marshal.ReadInt32(normalTokens, i * 4);
                    method_data_t currentData = new method_data_t(methodToken, 0, 0, 0, 0, 0);
                    DocumentHandle currentDocHandle = new DocumentHandle();

                    foreach (SequencePoint p in GetSequencePointCollection(methodToken, reader))
                    {
                        if (p.StartLine == 0 || p.StartLine == SequencePoint.HiddenLine)
                            continue;

                        // first access, init all fields and document with proper data from first user code sequence point
                        if (currentData.startLine == 0)
                        {
                            currentData.SetRange(p.StartLine, p.EndLine, p.StartColumn, p.EndColumn);
                            currentDocHandle = p.Document;
                            continue;
                        }

                        currentData.ExtendRange(p.StartLine, p.EndLine, p.StartColumn, p.EndColumn);
                    }

                    if (currentData.startLine != 0)
                    {
                        if (!moduleData.ContainsKey(currentDocHandle))
                            moduleData[currentDocHandle] = new List<method_data_t>();

                        moduleData[currentDocHandle].Add(currentData);
                    }
                }

                if (moduleData.Count == 0)
                    return RetCode.OK;

                int structModuleMethodsDataSize = Marshal.SizeOf<file_methods_data_t>();
                module_methods_data_t managedData;
                managedData.fileNum = moduleData.Count;
                managedData.moduleMethodsData = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(moduleData.Count * structModuleMethodsDataSize));
                IntPtr currentModuleMethodsDataPtr = managedData.moduleMethodsData;

                foreach (KeyValuePair<DocumentHandle, List<method_data_t>> fileData in moduleData)
                {
                    int structMethodDataSize = Marshal.SizeOf<method_data_t>();
                    file_methods_data_t fileMethodData;
                    fileMethodData.document = resourceTracker.TrackBSTR(Marshal.StringToBSTR(reader.GetString(reader.GetDocument(fileData.Key).Name)));
                    fileMethodData.methodNum = fileData.Value.Count;
                    fileMethodData.methodsData = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(fileData.Value.Count * structMethodDataSize));
                    IntPtr currentMethodDataPtr = fileMethodData.methodsData;

                    foreach (var p in fileData.Value)
                    {
                        Marshal.StructureToPtr(p, currentMethodDataPtr, false);
                        currentMethodDataPtr = currentMethodDataPtr + structMethodDataSize;
                    }

                    Marshal.StructureToPtr(fileMethodData, currentModuleMethodsDataPtr, false);
                    currentModuleMethodsDataPtr = currentModuleMethodsDataPtr + structModuleMethodsDataSize;
                }

                IntPtr resultPtr = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(Marshal.SizeOf<module_methods_data_t>()));
                Marshal.StructureToPtr(managedData, resultPtr, false);
                data = resultPtr;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct resolved_bp_t)
    [StructLayout(LayoutKind.Sequential)]
    internal struct resolved_bp_t
    {
        public int startLine;
        public int endLine;
        public int ilOffset;
        public int methodToken;

        public resolved_bp_t(int startLine_, int endLine_, int ilOffset_, int methodToken_)
        {
            startLine = startLine_;
            endLine = endLine_;
            ilOffset = ilOffset_;
            methodToken = methodToken_;
        }
    }

    enum Position
    {
        First,
        Last
    };

    /// <summary>
    /// Resolve breakpoints.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="tokenNum">number of elements in Tokens</param>
    /// <param name="Tokens">array of method tokens, that have sequence point with sourceLine</param>
    /// <param name="sourceLine">initial source line for resolve</param>
    /// <param name="nestedToken">close nested token for sourceLine</param>
    /// <param name="Count">entry's count in data</param>
    /// <param name="data">pointer to memory with result</param>
    /// <returns>"Ok" if information is available</returns>
    internal static RetCode ResolveBreakPoints(IntPtr symbolReaderHandle, int tokenNum, IntPtr Tokens, int sourceLine,
                                               int nestedToken, out int Count,
                                               [MarshalAs(UnmanagedType.LPWStr)] string sourcePath, out IntPtr data)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        Count = 0;
        data = IntPtr.Zero;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                // In case nestedToken + sourceLine is part of constructor (tokenNum > 1) we could have cases:
                // 1. type FieldName1 = new Type();
                //    void MethodName() {}; type FieldName2 = new Type(); ...  <-- sourceLine
                // 2. type FieldName1 = new Type(); void MethodName() {}; ...  <-- sourceLine
                //    type FieldName2 = new Type();
                // In first case, we need setup breakpoint in nestedToken's method (MethodName in examples above), in second - ignore it.

                // In case nestedToken + sourceLine in normal method we could have cases:
                // 1. ... line without code ...                                <-- sourceLine
                //    void MethodName { ...
                // 2. ... line with code ... void MethodName { ...             <-- sourceLine
                // We need check if nestedToken's method code closer to sourceLine than code from methodToken's method.
                // If sourceLine closer to nestedToken's method code - setup breakpoint in nestedToken's method.

                SequencePoint SequencePointForSourceLine(Position reqPos, ref MetadataReader _reader, int methodToken)
                {
                    // Note, SequencePoints ordered by IL offsets, not by line numbers.
                    // For example, infinite loop `while(true)` will have IL offset after cycle body's code.
                    SequencePoint nearestSP = default;
                    bool found = false;

                    foreach (SequencePoint p in GetSequencePointCollection(methodToken, _reader))
                    {
                        if (p.StartLine == 0 || p.StartLine == SequencePoint.HiddenLine || p.EndLine < sourceLine)
                            continue;

                        // Note, in case of constructors, we must care about source too, since we may have situation when
                        // field/property have same line in another source.
                        var fileName = _reader.GetString(_reader.GetDocument(p.Document).Name);
                        if (fileName != sourcePath)
                            continue;

                        if (!found)
                        {
                            nearestSP = p;
                            found = true;
                            continue;
                        }

                        if (p.EndLine != nearestSP.EndLine)
                        {
                            if ((reqPos == Position.First && p.EndLine < nearestSP.EndLine) ||
                                (reqPos == Position.Last && p.EndLine > nearestSP.EndLine))
                            {
                                nearestSP = p;
                            }
                        }
                        else
                        {
                            if ((reqPos == Position.First && p.EndColumn < nearestSP.EndColumn) ||
                                (reqPos == Position.Last && p.EndColumn > nearestSP.EndColumn))
                            {
                                nearestSP = p;
                            }
                        }
                    }

                    return nearestSP;
                }

                var list = new List<resolved_bp_t>();
                for (int i = 0; i < tokenNum; i++)
                {
                    int methodToken = Marshal.ReadInt32(Tokens, i * 4);
                    SequencePoint current_p = SequencePointForSourceLine(Position.First, ref reader, methodToken);
                    // Note, we don't check that current_p was found or not, since we know for sure, that sourceLine could be resolved in method.
                    // Same idea for nested_p below, if we have nestedToken - it will be resolved for sure.

                    if (nestedToken != 0)
                    {
                        // Check if nestedToken is within range of current_p. Example -
                        //     await Parallel.ForEachAsync(userHandlers, parallelOptions, async (uri, token) =>   <- breakpoint at this line
                        //     {
                        //        await new HttpClient().GetAsync("https://google.com");
                        //     });
                        // nestedToken here is the anonymous async func, and having a breakpoint at the 1st line should
                        // break on the outer call.
                        SequencePoint nested_start_p = SequencePointForSourceLine(Position.First, ref reader, nestedToken);
                        SequencePoint nested_end_p = SequencePointForSourceLine(Position.Last, ref reader, nestedToken);
                        if ((nested_start_p.StartLine > current_p.StartLine || (nested_start_p.StartLine == current_p.StartLine && nested_start_p.StartColumn > current_p.StartColumn)) &&
                            (nested_end_p.EndLine < current_p.EndLine || (nested_end_p.EndLine == current_p.EndLine && nested_end_p.EndColumn < current_p.EndColumn)))
                        {
                            list.Add(new resolved_bp_t(current_p.StartLine, current_p.EndLine, current_p.Offset, methodToken));
                            break;
                        }

                        // Note, sequence points can't partially overlap each other, since same lemmas can't belong to 2 different sequence points for sure.
                        // In this case we could check not "line" (start line - end line datas) but only "point" (end line data) for
                        // current method sequence point and first nested method sequence point.
                        if (current_p.EndLine > nested_start_p.EndLine || (current_p.EndLine == nested_start_p.EndLine && current_p.EndColumn > nested_start_p.EndColumn))
                        {
                            list.Add(new resolved_bp_t(nested_start_p.StartLine, nested_start_p.EndLine, nested_start_p.Offset, nestedToken));
                            // (tokenNum > 1) can have only lines, that added to multiple constructors, in this case - we will have same for all Tokens,
                            // we need unique tokens only for breakpoints, prevent adding nestedToken multiple times.
                            break;
                        }
                    }
                    nestedToken = 0; // Don't check nested block next cycle (will have same results).

                    list.Add(new resolved_bp_t(current_p.StartLine, current_p.EndLine, current_p.Offset, methodToken));
                }

                if (list.Count == 0)
                    return RetCode.OK;

                int structSize = Marshal.SizeOf<resolved_bp_t>();
                IntPtr allocatedMemory = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(list.Count * structSize));
                IntPtr dataPtr = allocatedMemory;

                foreach (var p in list)
                {
                    Marshal.StructureToPtr(p, dataPtr, false);
                    dataPtr += structSize;
                }

                data = allocatedMemory;
                Count = list.Count;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    internal static RetCode GetStepRangesFromIP(IntPtr symbolReaderHandle, uint ip, int methodToken,
                                                out uint ilStartOffset, out uint ilEndOffset)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        ilStartOffset = 0;
        ilEndOffset = 0;

        try
        {
            if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                return RetCode.Fail;

            var list = new List<SequencePoint>();
            foreach (SequencePoint p in GetSequencePointCollection(methodToken, reader))
                list.Add(p);

            var pointsArray = list.ToArray();

            for (int i = 1; i < pointsArray.Length; i++)
            {
                SequencePoint p = pointsArray[i];

                if (p.Offset > ip && p.StartLine != 0 && p.StartLine != SequencePoint.HiddenLine)
                {
                    ilStartOffset = (uint)pointsArray[0].Offset;
                    for (int j = i - 1; j > 0; j--)
                    {
                        if (pointsArray[j].Offset <= ip)
                        {
                            ilStartOffset = (uint)pointsArray[j].Offset;
                            break;
                        }
                    }
                    ilEndOffset = (uint)p.Offset;
                    return RetCode.OK;
                }
            }

            // let's handle correctly last step range from last sequence point till
            // end of the method.
            if (pointsArray.Length > 0)
            {
                ilStartOffset = (uint)pointsArray[0].Offset;
                for (int j = pointsArray.Length - 1; j > 0; j--)
                {
                    if (pointsArray[j].Offset <= ip)
                    {
                        ilStartOffset = (uint)pointsArray[j].Offset;
                        break;
                    }
                }
                ilEndOffset = ilStartOffset; // Should set this to IL code size in calling code
                return RetCode.OK;
            }
        }
        catch
        {
            return RetCode.Exception;
        }

        return RetCode.Fail;
    }

    internal static RetCode GetLocalVariableNameAndScope(IntPtr symbolReaderHandle, int methodToken, uint localIndex,
                                                         out IntPtr localVarName, out int ilStartOffset, out int ilEndOffset)
    {
        localVarName = IntPtr.Zero;
        ilStartOffset = 0;
        ilEndOffset = 0;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!GetLocalVariableAndScopeByIndex(symbolReaderHandle, methodToken, localIndex, out string localVar, out ilStartOffset, out ilEndOffset))
                    return RetCode.Fail;

                IntPtr allocatedBSTR = resourceTracker.TrackBSTR(Marshal.StringToBSTR(localVar));
                localVarName = allocatedBSTR;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct LocalConstantInfo)
    [StructLayout(LayoutKind.Sequential)]
    internal struct LocalConstantInfo
    {
        public IntPtr name;
        public IntPtr signature; // pointer to signature blob
        public int signatureSize;
    }

    /// <summary>
    /// Returns local constants for a method within the specified IL offset range.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="ilOffset">IL offset to filter constants in scope</param>
    /// <param name="constants">pointer to memory with local constant info array</param>
    /// <param name="constantCount">number of constants returned</param>
    /// <returns>"Ok" if information is available</returns>
    internal static RetCode GetLocalConstants(IntPtr symbolReaderHandle, int methodToken, uint ilOffset,
                                              out IntPtr constants, out int constantCount)
    {
        constants = IntPtr.Zero;
        constantCount = 0;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
                if (handle.Kind != HandleKind.MethodDefinition)
                    return RetCode.Fail;

                MethodDebugInformationHandle methodDebugHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
                LocalScopeHandleCollection localScopes = reader.GetLocalScopes(methodDebugHandle);

                var constantList = new List<LocalConstantInfo>();

                foreach (LocalScopeHandle scopeHandle in localScopes)
                {
                    LocalScope scope = reader.GetLocalScope(scopeHandle);

                    // Check if IL offset is within this scope
                    if (ilOffset < scope.StartOffset || ilOffset >= scope.EndOffset)
                        continue;

                    // Enumerate local constants in this scope
                    foreach (LocalConstantHandle constHandle in scope.GetLocalConstants())
                    {
                        LocalConstant localConst = reader.GetLocalConstant(constHandle);
                        string constName = reader.GetString(localConst.Name);

                        // Get the signature blob
                        BlobReader signatureReader = reader.GetBlobReader(localConst.Signature);

                        // Allocate memory for signature
                        IntPtr signaturePtr = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(signatureReader.Length));
                        byte[] signatureBytes = signatureReader.ReadBytes(signatureReader.Length);
                        Marshal.Copy(signatureBytes, 0, signaturePtr, signatureReader.Length);

                        var constInfo = new LocalConstantInfo
                        {
                            name = resourceTracker.TrackBSTR(Marshal.StringToBSTR(constName)),
                            signature = signaturePtr,
                            signatureSize = signatureReader.Length
                        };

                        constantList.Add(constInfo);
                    }
                }

                if (constantList.Count == 0)
                    return RetCode.OK;

                int structSize = Marshal.SizeOf<LocalConstantInfo>();
                IntPtr allocatedMemory = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(constantList.Count * structSize));
                IntPtr dataPtr = allocatedMemory;

                foreach (var constInfo in constantList)
                {
                    Marshal.StructureToPtr(constInfo, dataPtr, false);
                    dataPtr += structSize;
                }

                constants = allocatedMemory;
                constantCount = constantList.Count;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    internal static bool GetLocalVariableAndScopeByIndex(IntPtr symbolReaderHandle, int methodToken, uint localIndex,
                                                         out string localVarName, out int ilStartOffset, out int ilEndOffset)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        localVarName = null;
        ilStartOffset = 0;
        ilEndOffset = 0;

        try
        {
            GCHandle gch = GCHandle.FromIntPtr(symbolReaderHandle);
            MetadataReader reader = ((OpenedReader)gch.Target).Reader;

            Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
            if (handle.Kind != HandleKind.MethodDefinition)
                return false;

            MethodDebugInformationHandle methodDebugHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
            LocalScopeHandleCollection localScopes = reader.GetLocalScopes(methodDebugHandle);
            foreach (LocalScopeHandle scopeHandle in localScopes)
            {
                LocalScope scope = reader.GetLocalScope(scopeHandle);
                LocalVariableHandleCollection localVars = scope.GetLocalVariables();
                foreach (LocalVariableHandle varHandle in localVars)
                {
                    LocalVariable localVar = reader.GetLocalVariable(varHandle);
                    if (localVar.Index == localIndex)
                    {
                        if (localVar.Attributes == LocalVariableAttributes.DebuggerHidden)
                            return false;

                        localVarName = reader.GetString(localVar.Name);
                        ilStartOffset = scope.StartOffset;
                        ilEndOffset = scope.EndOffset;
                        return true;
                    }
                }
            }
        }
        catch
        {
        }

        return false;
    }

    /// <summary>
    /// Returns local variable name for given local index and IL offset.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="data">pointer to memory with hoisted local scopes</param>
    /// <param name="hoistedLocalScopesCount">hoisted local scopes count</param>
    /// <returns>"Ok" if information is available</returns>
    internal static RetCode GetHoistedLocalScopes(IntPtr symbolReaderHandle, int methodToken, out IntPtr data, out int hoistedLocalScopesCount)
    {
        data = IntPtr.Zero;
        hoistedLocalScopesCount = 0;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
                if (handle.Kind != HandleKind.MethodDefinition)
                    return RetCode.Fail;

                MethodDebugInformationHandle methodDebugInformationHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
                var entityHandle = MetadataTokens.EntityHandle(MetadataTokens.GetToken(methodDebugInformationHandle.ToDefinitionHandle()));

                // Guid is taken from Roslyn source code:
                // https://github.com/dotnet/roslyn/blob/afd10305a37c0ffb2cfb2c2d8446154c68cfa87a/src/Dependencies/CodeAnalysis.Debugging/PortableCustomDebugInfoKinds.cs#L14
                Guid stateMachineHoistedLocalScopes = new Guid("6DA9A61E-F8C7-4874-BE62-68BC5630DF71");

                var hoistedLocalScopes = new List<uint>();
                foreach (var cdiHandle in reader.GetCustomDebugInformation(entityHandle))
                {
                    var cdi = reader.GetCustomDebugInformation(cdiHandle);

                    if (reader.GetGuid(cdi.Kind) == stateMachineHoistedLocalScopes)
                    {
                        // Format of this blob is taken from Roslyn source code:
                        // https://github.com/dotnet/roslyn/blob/afd10305a37c0ffb2cfb2c2d8446154c68cfa87a/src/Compilers/Core/Portable/PEWriter/MetadataWriter.PortablePdb.cs#L600

                        var blobReader = reader.GetBlobReader(cdi.Value);

                        while (blobReader.Offset < blobReader.Length)
                        {
                            hoistedLocalScopes.Add(blobReader.ReadUInt32()); // StartOffset
                            hoistedLocalScopes.Add(blobReader.ReadUInt32()); // Length
                        }
                    }
                }

                if (hoistedLocalScopes.Count == 0)
                    return RetCode.Fail;

                IntPtr allocatedMemory = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(hoistedLocalScopes.Count * 4));
                IntPtr dataPtr = allocatedMemory;
                foreach (var p in hoistedLocalScopes)
                {
                    Marshal.StructureToPtr(p, dataPtr, false);
                    dataPtr += 4;
                }
                data = allocatedMemory;
                hoistedLocalScopesCount = hoistedLocalScopes.Count / 2;

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    /// <summary>
    /// Returns local variable name for given local index and IL offset.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="localIndex">local variable index</param>
    /// <param name="localVarName">local variable name return</param>
    /// <returns>true if name has been found</returns>
    internal static bool GetLocalVariableName(IntPtr symbolReaderHandle, int methodToken, int localIndex, out IntPtr localVarName)
    {
        localVarName = IntPtr.Zero;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!GetLocalVariableByIndex(symbolReaderHandle, methodToken, localIndex, out string localVar))
                    return false;

                IntPtr allocatedBSTR = resourceTracker.TrackBSTR(Marshal.StringToBSTR(localVar));
                localVarName = allocatedBSTR;

                resourceTracker.TransferOwnership();
                return true;
            }
            catch
            {
                return false;
            }
        }
    }

    /// <summary>
    /// Helper method to return local variable name for given local index and IL offset.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="localIndex">local variable index</param>
    /// <param name="localVarName">local variable name return</param>
    /// <returns>true if name has been found</returns>
    internal static bool GetLocalVariableByIndex(IntPtr symbolReaderHandle, int methodToken, int localIndex, out string localVarName)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);
        localVarName = null;

        try
        {
            GCHandle gch = GCHandle.FromIntPtr(symbolReaderHandle);
            MetadataReader reader = ((OpenedReader)gch.Target).Reader;

            Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
            if (handle.Kind != HandleKind.MethodDefinition)
                return false;

            MethodDebugInformationHandle methodDebugHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
            LocalScopeHandleCollection localScopes = reader.GetLocalScopes(methodDebugHandle);
            foreach (LocalScopeHandle scopeHandle in localScopes)
            {
                LocalScope scope = reader.GetLocalScope(scopeHandle);
                LocalVariableHandleCollection localVars = scope.GetLocalVariables();
                foreach (LocalVariableHandle varHandle in localVars)
                {
                    LocalVariable localVar = reader.GetLocalVariable(varHandle);
                    if (localVar.Index == localIndex)
                    {
                        if (localVar.Attributes == LocalVariableAttributes.DebuggerHidden)
                            return false;

                        localVarName = reader.GetString(localVar.Name);
                        return true;
                    }
                }
            }
        }
        catch
        {
        }

        return false;
    }

    /// <summary>
    /// Returns the portable PDB reader for the assembly path
    /// </summary>
    /// <param name="assemblyPath">file path of the assembly or null if the module is in-memory or dynamic</param>
    /// <param name="isFileLayout">type of in-memory PE layout, if true, file based layout otherwise, loaded layout</param>
    /// <param name="peStream">optional in-memory PE stream</param>
    /// <param name="pdbStream">optional in-memory PDB stream</param>
    /// <returns>reader/provider wrapper instance</returns>
    /// <remarks>
    /// Assumes that neither PE image nor PDB loaded into memory can be unloaded or moved around.
    /// </remarks>
    private static OpenedReader GetReader(string assemblyPath, bool isFileLayout, Stream peStream, Stream pdbStream)
    {
        return (pdbStream != null) ? TryOpenReaderForInMemoryPdb(pdbStream)
                                   : TryOpenReaderFromAssembly(assemblyPath, isFileLayout, peStream);
    }

    private static OpenedReader TryOpenReaderForInMemoryPdb(Stream pdbStream)
    {
        Debug.Assert(pdbStream != null);

        byte[] buffer = new byte[sizeof(uint)];
        if (pdbStream.Read(buffer, 0, sizeof(uint)) != sizeof(uint))
        {
            return null;
        }
        uint signature = BitConverter.ToUInt32(buffer, 0);

        // quick check to avoid throwing exceptions below in common cases:
        const uint ManagedMetadataSignature = 0x424A5342;
        if (signature != ManagedMetadataSignature)
        {
            // not a Portable PDB
            return null;
        }

        OpenedReader result = null;
        MetadataReaderProvider provider = null;
        try
        {
            pdbStream.Position = 0;
            provider = MetadataReaderProvider.FromPortablePdbStream(pdbStream);
            result = new OpenedReader(provider, provider.GetMetadataReader(), null);
        }
        catch (Exception e) when (e is BadImageFormatException || e is IOException)
        {
            return null;
        }
        finally
        {
            if (result is null)
            {
                provider?.Dispose();
            }
        }

        return result;
    }

    private static OpenedReader TryOpenReaderFromAssembly(string assemblyPath, bool isFileLayout, Stream peStream)
    {
        if (assemblyPath is null && peStream is null)
            return null;

        PEStreamOptions options = isFileLayout ? PEStreamOptions.Default : PEStreamOptions.IsLoadedImage;
        Stream streamToDispose = null;
        bool createdStream = false;

        if (peStream is null)
        {
            peStream = TryOpenFile(assemblyPath);
            if (peStream is null)
                return null;

            streamToDispose = peStream;
            createdStream = true;
            options = PEStreamOptions.Default;
        }

        try
        {
            using (var peReader = new PEReader(peStream, options))
            {
                DebugDirectoryEntry codeViewEntry, embeddedPdbEntry;
                ReadPortableDebugTableEntries(peReader, out codeViewEntry, out embeddedPdbEntry);

                // First try .pdb file specified in CodeView data (we prefer .pdb file on disk over embedded PDB
                // since embedded PDB needs decompression which is less efficient than memory-mapping the file).
                if (codeViewEntry.DataSize != 0)
                {
                    var result = TryOpenReaderFromCodeView(peReader, codeViewEntry, assemblyPath);
                    if (result != null)
                    {
                        return result;
                    }
                }

                // if it failed try Embedded Portable PDB (if available):
                if (embeddedPdbEntry.DataSize != 0)
                {
                    return TryOpenReaderFromEmbeddedPdb(peReader, embeddedPdbEntry);
                }
            }
        }
        catch (Exception e) when (e is BadImageFormatException || e is IOException)
        {
            // nop
        }
        finally
        {
            if (createdStream)
            {
                streamToDispose?.Dispose();
            }
        }

        return null;
    }

    private static void ReadPortableDebugTableEntries(PEReader peReader, out DebugDirectoryEntry codeViewEntry, out DebugDirectoryEntry embeddedPdbEntry)
    {
        // See spec: https://github.com/dotnet/corefx/blob/master/src/System.Reflection.Metadata/specs/PE-COFF.md

        codeViewEntry = default(DebugDirectoryEntry);
        embeddedPdbEntry = default(DebugDirectoryEntry);

        foreach (DebugDirectoryEntry entry in peReader.ReadDebugDirectory())
        {
            if (entry.Type == DebugDirectoryEntryType.CodeView)
            {
                const ushort PortableCodeViewVersionMagic = 0x504d;
                if (entry.MinorVersion != PortableCodeViewVersionMagic)
                {
                    continue;
                }

                codeViewEntry = entry;
            }
            else if (entry.Type == DebugDirectoryEntryType.EmbeddedPortablePdb)
            {
                embeddedPdbEntry = entry;
            }
        }
    }

    private static OpenedReader TryOpenReaderFromCodeView(PEReader peReader, DebugDirectoryEntry codeViewEntry, string assemblyPath)
    {
        OpenedReader result = null;
        MetadataReaderProvider provider = null;
        try
        {
            var data = peReader.ReadCodeViewDebugDirectoryData(codeViewEntry);

            string pdbPath = data.Path;
            if (assemblyPath != null)
            {
                try
                {
                    pdbPath = Path.Combine(Path.GetDirectoryName(assemblyPath), GetFileName(data.Path));
                }
                catch
                {
                    // invalid characters in CodeView path
                    return null;
                }
            }

            var pdbStream = TryOpenFile(pdbPath);
            if (pdbStream is null && assemblyPath != null)
            {
                // Workaround, since NI file could be generated in `.native_image` subdirectory.
                // Note: this is a temporary solution until we add an option for specifying the PDB path.
                try
                {
                    int tmpLastIndex = assemblyPath.LastIndexOf(".native_image");
                    if (tmpLastIndex == -1)
                    {
                        return null;
                    }
                    string tmpPath = assemblyPath.Substring(0, tmpLastIndex);
                    pdbPath = Path.Combine(Path.GetDirectoryName(tmpPath), GetFileName(data.Path));
                }
                catch
                {
                    // invalid characters in CodeView path
                    return null;
                }

                pdbStream = TryOpenFile(pdbPath);
            }
            if (pdbStream is null)
            {
                return null;
            }

            provider = MetadataReaderProvider.FromPortablePdbStream(pdbStream);
            var reader = provider.GetMetadataReader();

            // Validate that the PDB matches the assembly version
            if (data.Age == 1 && new BlobContentId(reader.DebugMetadataHeader.Id) == new BlobContentId(data.Guid, codeViewEntry.Stamp))
            {
                result = new OpenedReader(provider, reader, pdbPath);
            }
        }
        catch (Exception e) when (e is BadImageFormatException || e is IOException)
        {
            return null;
        }
        finally
        {
            if (result is null)
            {
                provider?.Dispose();
            }
        }

        return result;
    }

    private static OpenedReader TryOpenReaderFromEmbeddedPdb(PEReader peReader, DebugDirectoryEntry embeddedPdbEntry)
    {
        OpenedReader result = null;
        MetadataReaderProvider provider = null;

        try
        {
            // TODO: We might want to cache this provider globally (across stack traces),
            // since decompressing embedded PDB takes some time.
            provider = peReader.ReadEmbeddedPortablePdbDebugDirectoryData(embeddedPdbEntry);
            result = new OpenedReader(provider, provider.GetMetadataReader(), null);
        }
        catch (Exception e) when (e is BadImageFormatException || e is IOException)
        {
            return null;
        }
        finally
        {
            if (result is null)
            {
                provider?.Dispose();
            }
        }

        return result;
    }

    private static Stream TryOpenFile(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return null;
        }
        try
        {
            return File.OpenRead(path);
        }
        catch
        {
            return null;
        }
    }

    // WARNING: Keep this struct in sync with src/managed/interop.h (struct AsyncAwaitInfoBlock)
    [StructLayout(LayoutKind.Sequential)]
    internal struct AsyncAwaitInfoBlock
    {
        public uint yield_offset;
        public uint resume_offset;
        public uint token;
    }

    /// <summary>
    /// Helper method to return async method stepping information and return last method's offset for user code.
    /// </summary>
    /// <param name="symbolReaderHandle">symbol reader handle returned by LoadSymbolsForModule</param>
    /// <param name="methodToken">method token</param>
    /// <param name="asyncInfo">array with all async method stepping information</param>
    /// <param name="asyncInfoCount">entry's count in asyncInfo</param>
    /// <param name="LastIlOffset">return last found IL offset in user code</param>
    /// <returns>"Ok" if method have at least one await block and last IL offset was found</returns>
    internal static RetCode GetAsyncMethodSteppingInfo(IntPtr symbolReaderHandle, int methodToken, out IntPtr asyncInfo, out int asyncInfoCount, out uint LastIlOffset)
    {
        Debug.Assert(symbolReaderHandle != IntPtr.Zero);

        asyncInfo = IntPtr.Zero;
        asyncInfoCount = 0;
        LastIlOffset = 0;

        using (var resourceTracker = new ResourceTracker())
        {
            try
            {
                if (!TryGetReaderWithValidation(symbolReaderHandle, out MetadataReader reader))
                    return RetCode.Fail;

                Handle handle = GetDeltaRelativeMethodDefinitionHandle(reader, methodToken);
                if (handle.Kind != HandleKind.MethodDefinition)
                    return RetCode.Fail;

                MethodDebugInformationHandle methodDebugInformationHandle = ((MethodDefinitionHandle)handle).ToDebugInformationHandle();
                var entityHandle = MetadataTokens.EntityHandle(MetadataTokens.GetToken(methodDebugInformationHandle.ToDefinitionHandle()));

                // Guid is taken from Roslyn source code:
                // https://github.com/dotnet/roslyn/blob/afd10305a37c0ffb2cfb2c2d8446154c68cfa87a/src/Dependencies/CodeAnalysis.Debugging/PortableCustomDebugInfoKinds.cs#L13
                Guid asyncMethodSteppingInformationBlob = new Guid("54FD2AC5-E925-401A-9C2A-F94F171072F8");

                var list = new List<AsyncAwaitInfoBlock>();
                foreach (var cdiHandle in reader.GetCustomDebugInformation(entityHandle))
                {
                    var cdi = reader.GetCustomDebugInformation(cdiHandle);

                    if (reader.GetGuid(cdi.Kind) == asyncMethodSteppingInformationBlob)
                    {
                        // Format of this blob is taken from Roslyn source code:
                        // https://github.com/dotnet/roslyn/blob/afd10305a37c0ffb2cfb2c2d8446154c68cfa87a/src/Compilers/Core/Portable/PEWriter/MetadataWriter.PortablePdb.cs#L575

                        var blobReader = reader.GetBlobReader(cdi.Value);
                        blobReader.ReadUInt32(); // skip catch_handler_offset

                        while (blobReader.Offset < blobReader.Length)
                        {
                            list.Add(new AsyncAwaitInfoBlock() {
                                yield_offset = blobReader.ReadUInt32(),
                                resume_offset = blobReader.ReadUInt32(),
                                // explicit conversion from int into uint here, see:
                                // https://docs.microsoft.com/en-us/dotnet/api/system.reflection.metadata.blobreader.readcompressedinteger
                                token = (uint)blobReader.ReadCompressedInteger()
                            });
                        }
                    }
                }

                if (list.Count == 0)
                    return RetCode.Fail;

                int structSize = Marshal.SizeOf<AsyncAwaitInfoBlock>();
                IntPtr allocatedMemory = resourceTracker.TrackMemory(Marshal.AllocCoTaskMem(list.Count * structSize));
                IntPtr currentPtr = allocatedMemory;

                foreach (var p in list)
                {
                    Marshal.StructureToPtr(p, currentPtr, false);
                    currentPtr += structSize;
                }

                asyncInfo = allocatedMemory;
                asyncInfoCount = list.Count;

                // We don't use LINQ in order to reduce memory consumption for managed part, so, Reverse() usage not an option here.
                // Note, SequencePointCollection is IEnumerable based collections.
                bool foundOffset = false;
                foreach (SequencePoint p in GetSequencePointCollection(methodToken, reader))
                {
                    if (p.StartLine == 0 || p.StartLine == SequencePoint.HiddenLine || p.Offset < 0)
                        continue;

                    // Method's IL start only from 0, use uint for IL offset.
                    LastIlOffset = (uint)p.Offset;
                    foundOffset = true;
                }

                if (!foundOffset)
                {
                    return RetCode.Fail;
                }

                resourceTracker.TransferOwnership();
                return RetCode.OK;
            }
            catch
            {
                return RetCode.Exception;
            }
        }
    }

    #endregion // Resource Management
}
}
