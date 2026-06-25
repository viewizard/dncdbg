// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/async_info.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdbreader.h"

namespace dncdbg
{

// Caller must hold m_asyncMethodSteppingInfoMutex.
HRESULT AsyncInfo::GetAsyncMethodSteppingInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken)
{
    // Note, for normal methods, `PDBReader::GetAsyncMethodSteppingInfo()` will return error code and set `lastIlOffset`
    // to 0. Error during async info search (debug info not available or method token belong to normal method) is proper
    // behavior and debugger logic also count on this.

    if (asyncMethodSteppingInfo.modAddress == modAddress && asyncMethodSteppingInfo.methodToken == methodToken)
    {
        return asyncMethodSteppingInfo.retCode;
    }

    if (!asyncMethodSteppingInfo.awaits.empty())
    {
        asyncMethodSteppingInfo.awaits.clear();
    }

    asyncMethodSteppingInfo.modAddress = modAddress;
    asyncMethodSteppingInfo.methodToken = methodToken;
    asyncMethodSteppingInfo.retCode = m_sharedDebugInfo->GetPDBInfo(modAddress,
        [&](PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetAsyncMethodSteppingInfo(pdbInfo.m_pdbHandle, methodToken, asyncMethodSteppingInfo.catchHandlerOffset,
                                                         asyncMethodSteppingInfo.awaits, asyncMethodSteppingInfo.lastIlOffset);
        });

    return asyncMethodSteppingInfo.retCode;
}

// Check if method have await block. In this way we detect async method with awaits.
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
bool AsyncInfo::IsMethodHaveAwait(CORDB_ADDRESS modAddress, mdMethodDef methodToken)
{
    const std::scoped_lock<std::mutex> lock(m_asyncMethodSteppingInfoMutex);

    return SUCCEEDED(GetAsyncMethodSteppingInfo(modAddress, methodToken));
}

// Find await block after IL offset in particular async method and return await info, if present.
// In case of async stepping, we need await info from PDB in order to setup breakpoints in proper places (yield and resume offsets).
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
// [in] ipOffset - IL offset;
// [out] awaitInfo - result, next await info.
bool AsyncInfo::FindNextAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ipOffset, PDB::AsyncAwaitInfoBlock &awaitInfo)
{
    const std::scoped_lock<std::mutex> lock(m_asyncMethodSteppingInfoMutex);

    if (FAILED(GetAsyncMethodSteppingInfo(modAddress, methodToken)))
    {
        return false;
    }

    for (auto &await : asyncMethodSteppingInfo.awaits)
    {
        if (ipOffset <= await.yieldOffset)
        {
            awaitInfo = await;
            return true;
        }
        // Stop search, if IP inside 'await' routine.
        else if (ipOffset < await.resumeOffset)
        {
            break;
        }
    }

    return false;
}

// Find last IL offset for user code in async method, if present.
// In case of step-in and step-over we must detect last user code line in order to "emulate"
// step-out (DebuggerOfWaitCompletion magic) instead.
// [in] modAddress - module address;
// [in] methodToken - method token (from module with address modAddress).
// [out] lastIlOffset - result, IL offset for last user code line in async method.
bool AsyncInfo::FindLastIlOffsetAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t &lastIlOffset)
{
    const std::scoped_lock<std::mutex> lock(m_asyncMethodSteppingInfoMutex);

    if (FAILED(GetAsyncMethodSteppingInfo(modAddress, methodToken)))
    {
        return false;
    }

    lastIlOffset = asyncMethodSteppingInfo.lastIlOffset;
    return true;
}

} // namespace dncdbg
