// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/async_info.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdbreader.h"
#include "utils/hresult.h"
#include <mutex>
#include <vector>

namespace dncdbg::AsyncInfo
{

namespace
{

struct AsyncMethodInfo
{
    CORDB_ADDRESS modAddress{0};
    mdMethodDef methodToken{mdMethodDefNil};
    HRESULT retCode{S_OK};

    std::vector<PDB::AsyncAwaitInfoBlock> awaits;
    // Part of the NotifyDebuggerOfWaitCompletion magic, see AsyncStepper::SetupStep().
    uint32_t lastIlOffset{0};
};

AsyncMethodInfo &GetAsyncMethodSteppingInfoData()
{
    static AsyncMethodInfo asyncMethodSteppingInfo;
    return asyncMethodSteppingInfo;
}

std::mutex &GetAsyncMethodSteppingInfoMutex()
{
    static std::mutex asyncMethodSteppingInfoMutex;
    return asyncMethodSteppingInfoMutex;
}

// Caller must hold GetAsyncMethodSteppingInfoMutex().
// Note, the result is stored in GetAsyncMethodSteppingInfoData().
HRESULT GetAsyncMethodSteppingInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken)
{
    // Note, for normal methods, `PDBReader::GetAsyncMethodSteppingInfo()` returns an error code.
    // An error during the async info search (debug info not available, or the method token belongs to a normal
    // method) is expected behavior, and the debugger logic relies on this.

    AsyncMethodInfo &asyncMethodSteppingInfo = GetAsyncMethodSteppingInfoData();

    if (asyncMethodSteppingInfo.modAddress == modAddress && asyncMethodSteppingInfo.methodToken == methodToken)
    {
        return asyncMethodSteppingInfo.retCode;
    }

    asyncMethodSteppingInfo.awaits.clear();

    asyncMethodSteppingInfo.modAddress = modAddress;
    asyncMethodSteppingInfo.methodToken = methodToken;
    asyncMethodSteppingInfo.retCode = DebugInfo::GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            HRESULT Status = S_OK;
            IfFailRet(PDBReader::GetAsyncMethodSteppingInfo(pdbInfo.m_pdbHandle, methodToken, asyncMethodSteppingInfo.awaits));
            return PDBReader::GetLastIlOffset(pdbInfo.m_pdbHandle, methodToken, asyncMethodSteppingInfo.lastIlOffset);
        });

    return asyncMethodSteppingInfo.retCode;
}

} // unnamed namespace

// Check if the method has an await block; this is how we detect async methods with awaits.
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
bool IsMethodHaveAwait(CORDB_ADDRESS modAddress, mdMethodDef methodToken)
{
    const std::scoped_lock<std::mutex> lock(GetAsyncMethodSteppingInfoMutex());

    return SUCCEEDED(GetAsyncMethodSteppingInfo(modAddress, methodToken));
}

// Find an await block after the IL offset in a particular async method and return the await info, if present.
// For async stepping, we need await info from the PDB to set up breakpoints in the proper places (yield and resume offsets).
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
// [in] ipOffset - IL offset;
// [out] awaitInfo - result, next await info.
bool FindNextAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ipOffset, PDB::AsyncAwaitInfoBlock &awaitInfo)
{
    const std::scoped_lock<std::mutex> lock(GetAsyncMethodSteppingInfoMutex());

    if (FAILED(GetAsyncMethodSteppingInfo(modAddress, methodToken)))
    {
        return false;
    }

    const auto &awaits = GetAsyncMethodSteppingInfoData().awaits;
    for (const auto &await : awaits)
    {
        if (ipOffset <= await.yieldOffset)
        {
            awaitInfo = await;
            return true;
        }
        // Stop the search if the IP is inside the 'await' routine.
        else if (ipOffset < await.resumeOffset)
        {
            break;
        }
    }

    return false;
}

// Find the last IL offset for user code in an async method, if present.
// For step-in and step-over, we must detect the last user code line in order to "emulate"
// step-out (NotifyDebuggerOfWaitCompletion magic) instead.
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
// [out] lastIlOffset - result, IL offset for last user code line in async method.
bool FindLastIlOffsetAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t &lastIlOffset)
{
    const std::scoped_lock<std::mutex> lock(GetAsyncMethodSteppingInfoMutex());

    if (FAILED(GetAsyncMethodSteppingInfo(modAddress, methodToken)))
    {
        return false;
    }

    lastIlOffset = GetAsyncMethodSteppingInfoData().lastIlOffset;
    return true;
}

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetAsyncMethodSteppingInfoMutex());
    GetAsyncMethodSteppingInfoData() = AsyncMethodInfo{};
}

} // namespace dncdbg::AsyncInfo
