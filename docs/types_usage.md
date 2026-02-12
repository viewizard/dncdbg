## C++ types usage in code.

C++ standard fixed width integer types and STL containers usage in code preferable.

**For interaction with MS API (in order to support MSVS build on Windows) allowed limited MS type set:**
`BOOL`, `BOOLEAN`, `BYTE`, `SHORT`, `USHORT`, `INT`, `UINT`, `LONG`, `ULONG`, `LONGLONG`, `ULONGLONG`, `WORD`, `DWORD`, `DWORDLONG`, `CHAR`, `UCHAR`, `WCHAR`, `SIZE_T` , `SSIZE_T`.
As an exception, `LPWSTR` usage allowed in order to prevent `WCHAR ***` (`LPWSTR **`) MS API function parameters.
***Note, MS types should not used out of MS API related code.***

**Not allowed in code:**
- all MS pointer types (usually have prefix `P` or `LP`), should be replaced with normal type with pointer `*`.
- all MS const types (have `C` letter in name), should be replaced with normal type and `const` keyword.
- all MS fixed width integer types, should be replaced with C++ standard fixed width integer types.

**Conversion for not allowed MS types:**

`VOID` => `void`
`PVOID`, `LPVOID` => `void *`
`LPCVOID` => `const void *`

`FLOAT` => `float`
`DOUBLE` => `double`

`PBYTE`, `LPBYTE` => `BYTE *`
`LPCBYTE` => `const BYTE *`
`PSHORT` => `SHORT *`
`PUSHORT` => `USHORT *`
`PINT`, `LPINT` => `INT *`
`PUINT` => `UINT *`
`PLONG`, `LPLONG` => `LONG *`
`PULONG` => `ULONG *`
`PULONGLONG` => `ULONGLONG *`
`PWORD`, `LPWORD` => `WORD *`
`PDWORD`, `LPDWORD` => `DWORD *`

`DWORD32` => `uint32_t`
`PDWORD32` => `uint32_t *`
`DWORD64` => `uint64_t`
`PDWORD64` => `uint64_t *`
`LONG32` => `int32_t`
`PLONG32` => `int32_t *`
`ULONG32` => `uint32_t`
`PULONG32` => `uint32_t *`
`LONG64` => `int64_t`
`PLONG64` => `int64_t *`
`ULONG64`, => `uint64_t`
`PULONG64` => `uint64_t*`

`UINT8` => `uint8_t`
`INT8` => `int8_t`
`UINT16` => `uint16_t`
`INT16` => `int16_t`
`UINT32` => `uint32_t`
`PUINT32` => `uint32_t *`
`INT32` => `int32_t`
`PINT32` => `int32_t *`
`UINT64` => `uint64_t`
`PUINT64` => `uint64_t *`
`INT64` => `int64_t`
`PINT64` => `int64_t *`

`INT_PTR` => `intptr_t`
`PINT_PTR` => `intptr_t *`
`UINT_PTR` => `uintptr_t`
`PUINT_PTR` => `uintptr_t *`

`PWCHAR`, `LPWCH`, `PWCH`, `NWPSTR`, `PWSTR`, `LPWSTR` => `WCHAR *`
`LPCWCH`, `PCWCH`, `PCWSTR`, `LPCWSTR` => `const WCHAR *`

`PUCHAR` => `UCHAR *`
`PCHAR`, `PCH`, `LPCH`, `NPSTR`, `PSTR`, `LPSTR` => `CHAR *`
`PCCH`, `LPCCH`, `PCSTR`, `LPCSTR` = `const CHAR *`
