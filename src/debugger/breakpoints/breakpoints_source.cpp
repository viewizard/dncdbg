// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints_source.h"
#include "debugger/breakpoints/internal_helpers.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/evalhelpers/textwitheval.h"
#include "debugger/frames.h"
#include "debugger/threads.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdb.h"
#include "debuginfo/sourcereference.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "protocol/dap_events.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dncdbg::SourceBreakpoints
{

namespace
{

// Hash functor for std::pair<int32_t, int32_t> (line, column) key used in unordered containers.
struct LineColumnHash
{
    std::size_t operator()(const std::pair<int32_t, int32_t> &key) const
    {
        const std::size_t h1 = std::hash<int32_t>{}(key.first);
        const std::size_t h2 = std::hash<int32_t>{}(key.second);
        return h1 ^ (h2 << 1U);
    }
};

std::mutex &GetBreakpointsMutex()
{
    static std::mutex breakpointsMutex;
    return breakpointsMutex;
}

struct ManagedSourceBreakpoint
{
    uint32_t id{0};
    int32_t lineNum{0};
    int32_t columnNum{0};
    int32_t endLine{0};
    int32_t endColumn{0};
    uint32_t hitCount{0};
    HitCondition hitCondition;
    std::string condition;
    std::string logMessage;
    // Parsed logMessage string, each entry is a pair: <text, isExpression>.
    std::vector<std::pair<std::string, bool>> logMessageParts;
    // Set when the parsed logMessage has at least one special token in its expression parts (see ExpressionHasSpecialTokens()).
    bool hasSpecialTokens{false};
    // In case of a code line in a constructor, we could resolve multiple methods for the breakpoint.
    // For example, `MyType obj = new MyType(1);` will be added to all class constructors.
    std::vector<std::pair<ToRelease<ICorDebugFunctionBreakpoint>, CORDB_ADDRESS>> trFuncBreakpoints;

    [[nodiscard]] bool IsVerified() const
    {
        return !trFuncBreakpoints.empty();
    }

    ManagedSourceBreakpoint() = default;
    ~ManagedSourceBreakpoint();

    void ToBreakpoint(Breakpoint &breakpoint, const std::string &sourceFile, int32_t sourceReference,
                      const std::string *pAlgorithm = nullptr, const std::string *pChecksum = nullptr) const;

    ManagedSourceBreakpoint(ManagedSourceBreakpoint &&) = default;
    ManagedSourceBreakpoint(const ManagedSourceBreakpoint &) = delete;
    ManagedSourceBreakpoint &operator=(ManagedSourceBreakpoint &&) = default;
    ManagedSourceBreakpoint &operator=(const ManagedSourceBreakpoint &) = delete;
};

ManagedSourceBreakpoint::~ManagedSourceBreakpoint()
{
    for (auto &[trFuncBreakpoint, nativeAddress] : trFuncBreakpoints)
    {
        BreakpointHelpers::DeactivateManagedBreakpoint(trFuncBreakpoint);
    }
}

void ManagedSourceBreakpoint::ToBreakpoint(Breakpoint &breakpoint, const std::string &sourceFile, int32_t sourceReference,
                                           const std::string *pAlgorithm, const std::string *pChecksum) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
    breakpoint.source = Source(sourceFile, sourceReference);
    if (pAlgorithm != nullptr && pChecksum != nullptr && !(*pAlgorithm).empty() && !(*pChecksum).empty())
    {
        breakpoint.source.checksums.emplace_back(*pAlgorithm, *pChecksum);
    }
    breakpoint.line = this->lineNum;
    breakpoint.column = this->columnNum;
    breakpoint.endLine = this->endLine;
    breakpoint.endColumn = this->endColumn;
}

struct ManagedSourceBreakpointMapping
{
    SourceBreakpoint breakpoint{0, 0};
    uint32_t id{0};
    PDB::GlobalFileIndex resolvedGlobalFileIndex{};
    int32_t sourceReference{0};
    std::vector<Checksum> checksums;
    int32_t resolvedLineNum{0}; // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()
    int32_t resolvedColumnNum{0}; // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()

    void Reset()
    {
        resolvedGlobalFileIndex = PDB::GlobalFileIndex{};
        resolvedLineNum = 0;
        resolvedColumnNum = 0;
    }

    ManagedSourceBreakpointMapping() = default;
    ManagedSourceBreakpointMapping(ManagedSourceBreakpointMapping &&) = default;
    ManagedSourceBreakpointMapping(const ManagedSourceBreakpointMapping &) = delete;
    ManagedSourceBreakpointMapping &operator=(ManagedSourceBreakpointMapping &&) = delete;
    ManagedSourceBreakpointMapping &operator=(const ManagedSourceBreakpointMapping &) = delete;

    ~ManagedSourceBreakpointMapping() = default;
};

// Resolved source breakpoints:
// Mapped for fast search, the mapping data is in the container below:
// resolved global source path index -> resolved (line, column) pair -> list of all ManagedSourceBreakpoint objects resolved to this line:column.
std::unordered_map<PDB::GlobalFileIndex, std::unordered_map<std::pair<int32_t, int32_t>,
                   std::list<ManagedSourceBreakpoint>, LineColumnHash>, PDB::GlobalFileIndexHash> &GetSourceResolvedBreakpoints()
{
    static std::unordered_map<PDB::GlobalFileIndex, std::unordered_map<std::pair<int32_t, int32_t>,
                              std::list<ManagedSourceBreakpoint>, LineColumnHash>, PDB::GlobalFileIndexHash> sourceResolvedBreakpoints;
    return sourceResolvedBreakpoints;
}

// Mapping for the input SourceBreakpoint array (input from protocol) to ManagedSourceBreakpoint or unresolved breakpoint.
// Note, unlike FunctionBreakpoints, for a resolved breakpoint the source path and/or line number could have changed.
// In this way we can connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
// The container has a structure for fast comparison of the current breakpoint data with the new breakpoint data from the protocol:
// path to source -> list of ManagedSourceBreakpointMapping that includes SourceBreakpoint (from protocol) and resolution-related data.
std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> &GetSourceBreakpointMapping()
{
    static std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> sourceBreakpointMapping;
    return sourceBreakpointMapping;
}

// [in] pModule - optional, provide filter by module during resolve
// [in,out] bp - breakpoint data for resolve
HRESULT ResolveSourceBreakpoint(ICorDebugModule *pModule, const ManagedSourceBreakpoint &bp,
                                const Source &source, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints,
                                PDB::GlobalFileIndex &globalFileIndex)
{
    if (source.path.empty() || bp.lineNum <= 0 || bp.endLine <= 0)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;

    if (pModule != nullptr)
    {
        IfFailRet(pModule->GetBaseAddress(&modAddress));
    }

    IfFailRet(DebugInfo::ResolveBreakpoint(modAddress, source, bp.lineNum, bp.columnNum, &globalFileIndex, resolvedPoints));
    if (resolvedPoints.empty())
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT ActivateSourceBreakpoint(ManagedSourceBreakpoint &bp, const std::string &sourcePath,
                                 const std::vector<PDB::ResolvedBreakpoint> &resolvedPoints)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    CORDB_ADDRESS modAddressTrack = 0;
    bp.trFuncBreakpoints.reserve(resolvedPoints.size());
    for (const auto &resolvedBP : resolvedPoints)
    {
        // Note, we might have a situation with the same source path in different modules.
        // DAP and the internal debugger routine don't support this case.
        IfFailRet(resolvedBP.trModule->GetBaseAddress(&modAddressTrack));
        if ((modAddress != 0U) && (modAddress != modAddressTrack))
        {
            LOGW(log << "During breakpoint resolve, multiple modules with same source file path were detected.");
            LOGW(log << "File name: " << sourcePath);
            LOGW(log << "Breakpoint activated in module: " << Modules::GetModuleFilePath(resolvedPoints.at(0).trModule));
            LOGW(log << "Ignored module: " << Modules::GetModuleFilePath(resolvedBP.trModule));
            continue;
        }

        IfFailRet(BreakpointHelpers::SkipBreakpoint(resolvedBP.trModule, resolvedBP.methodToken));
        if (Status == S_SKIP)
        {
            continue;
        }

        modAddress = modAddressTrack;
        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(BreakpointHelpers::ActivateManagedBreakpoint(modAddress, resolvedBP.methodToken, resolvedBP.ilOffset,
                                                               resolvedBP.trModule, &trFuncBreakpoint));
        CORDB_ADDRESS nativeAddress = 0;
        BreakpointHelpers::GetBreakpointNativeAddress(trFuncBreakpoint, nativeAddress);
        bp.trFuncBreakpoints.emplace_back(trFuncBreakpoint.Detach(), nativeAddress);
    }

    if (modAddress == 0)
    {
        return E_FAIL;
    }

    // No reason to leave extra space here, since a breakpoint could be set up for 1 module only (no more breakpoints will be added).
    bp.trFuncBreakpoints.shrink_to_fit();

    // The same for multiple breakpoint resolution for one module.
    bp.lineNum = resolvedPoints.at(0).startLine;
    bp.columnNum = resolvedPoints.at(0).startColumn;
    bp.endLine = resolvedPoints.at(0).endLine;
    bp.endColumn = resolvedPoints.at(0).endColumn;

    return S_OK;
}

// Special tokens supported in the expression parts of a logpoint message (see ExpandLogMessageTokens()).
constexpr std::string_view filePosToken = "$FILEPOS";
constexpr std::string_view functionToken = "$FUNCTION";
constexpr std::string_view addressToken = "$ADDRESS";
constexpr std::string_view tidToken = "$TID";
constexpr std::string_view pidToken = "$PID";
constexpr std::string_view threadNameToken = "$TNAME";
constexpr std::string_view callerToken = "$CALLER";
constexpr std::string_view callstackToken = "$CALLSTACK";
constexpr std::string_view hitCountToken = "$HITCOUNT";

// All special tokens are listed here so that the expression parts of a logpoint message can be checked for them.
constexpr std::array<std::string_view, 9> specialTokens{
    filePosToken, functionToken, addressToken, tidToken, pidToken,
    threadNameToken, callerToken, callstackToken, hitCountToken};

// Returns true if any expression part of the parsed logpoint message contains the given token.
bool HasToken(const std::vector<std::pair<std::string, bool>> &logMessageParts, const std::string_view token)
{
    return std::any_of(logMessageParts.cbegin(), logMessageParts.cend(),
        [&token](const auto &logMessagePart)
        {
            return logMessagePart.second && logMessagePart.first.find(token) != std::string::npos;
        });
}

// Returns true if the parsed logpoint message contains at least one special token
// in its expression parts (the text between curly braces).
bool ExpressionHasSpecialTokens(const std::vector<std::pair<std::string, bool>> &logMessageParts)
{
    return std::any_of(specialTokens.cbegin(), specialTokens.cend(),
        [&logMessageParts](const std::string_view token)
        {
            return HasToken(logMessageParts, token);
        });
}

// Wraps the text in a C# verbatim string literal (@"..."), so the evaluated value matches the text as is.
// Embedded double quotes are doubled, as the literal syntax requires.
std::string ToVerbatimStringLiteral(const std::string &text)
{
    std::string result;
    result.reserve(text.size() + 3);
    result += "@\"";
    for (const char c : text)
    {
        if (c == '"')
        {
            result += "\"\"";
            continue;
        }

        result.push_back(c);
    }

    result += '"';
    return result;
}

// Returns the text with every special token occurrence replaced by its value.
// Tokens are expanded from left to right, so the substituted values themselves are never
// expanded again.
std::string ExpandSpecialTokens(const std::string &text,
                                const std::vector<std::pair<std::string_view, std::string>> &tokenValues)
{
    std::string result;
    std::string_view remaining = text;
    while (!remaining.empty())
    {
        std::size_t bestPos = std::string_view::npos;
        const std::pair<std::string_view, std::string> *bestMatch = nullptr;
        for (const auto &tokenValue : tokenValues)
        {
            const std::size_t foundPos = remaining.find(tokenValue.first);
            if (foundPos < bestPos)
            {
                bestPos = foundPos;
                bestMatch = &tokenValue;
            }
        }

        if (bestMatch == nullptr)
        {
            break;
        }

        result.append(remaining.substr(0, bestPos));
        result.append(bestMatch->second);
        remaining.remove_prefix(bestPos + bestMatch->first.length());
    }

    result.append(remaining);
    return result;
}

// Returns the fully qualified display name of the method executed by the frame (the user code, if the frame
// belongs to an async or compiler-generated state machine). Returns a placeholder if the name cannot be resolved.
std::string GetDisplayMethodName(ICorDebugFrame *pFrame)
{
    std::string displayMethodName;
    if (FAILED(EvalMetadataHelpers::GetFQDisplayRealCodeMethodName(pFrame, displayMethodName)))
    {
        displayMethodName = "[Unnamed managed method in optimized code]";
    }

    return displayMethodName;
}

// Returns the fully qualified display name of the method that called the frame (the user code, if the
// caller frame belongs to an async or compiler-generated state machine). Returns a placeholder if the
// caller cannot be resolved.
std::string GetCallerDisplayMethodName(ICorDebugFrame *pFrame)
{
    ToRelease<ICorDebugFrame> trCallerFrame;
    if (FAILED(pFrame->GetCaller(&trCallerFrame)) || trCallerFrame == nullptr)
    {
        return "[Unknown Frame]";
    }

    return GetDisplayMethodName(trCallerFrame);
}

// Returns the call stack of the thread as newline-separated frame names, starting from the current
// frame. Returns a placeholder if the stack cannot be retrieved.
std::string GetCallStack(ICorDebugThread *pThread)
{
    std::vector<StackFrame> stackFrames;
    const ThreadId threadId(Threads::GetId(pThread));

    if (FAILED(GetStackFrames(pThread, threadId, FrameLevel(0), 0, stackFrames)) || stackFrames.empty())
    {
        return "[Unknown Stack]";
    }

    std::string strStack;
    for (const auto &frame : stackFrames)
    {
        if (!strStack.empty())
        {
            strStack += '\n';
        }

        strStack += frame.name;
    }

    return strStack;
}

// Expands the special tokens of the current breakpoint hit in the expression parts of the parsed
// logpoint message. Text parts are copied as is. The result is stored in expandedLogMessageParts.
void ExpandLogMessageTokens(const ManagedSourceBreakpoint &b, ICorDebugThread *pThread, ICorDebugFrame *pFrame,
                            const std::string &sourceFilePath, CORDB_ADDRESS nativeAddress,
                            std::vector<std::pair<std::string, bool>> &expandedLogMessageParts)
{
    // $TID and $PID are replaced with pseudo-variables, which the evaluator resolves to the actual IDs.
    std::vector<std::pair<std::string_view, std::string>> tokenValues{
        {filePosToken, ToVerbatimStringLiteral(sourceFilePath + ':' + std::to_string(b.lineNum))},
        {addressToken, ToVerbatimStringLiteral(MetadataHelpers::AddrToString(nativeAddress))},
        {tidToken, "$tid"},
        {pidToken, "$pid"},
        {hitCountToken, std::to_string(b.hitCount)},
    };

    // Resolving the method, caller and thread names is relatively expensive, so resolve them only when
    // the message actually uses the corresponding tokens.
    if (HasToken(b.logMessageParts, functionToken))
    {
        tokenValues.emplace_back(functionToken, ToVerbatimStringLiteral(GetDisplayMethodName(pFrame)));
    }
    if (HasToken(b.logMessageParts, threadNameToken))
    {
        tokenValues.emplace_back(threadNameToken, ToVerbatimStringLiteral(Threads::GetThreadName(pThread)));
    }
    if (HasToken(b.logMessageParts, callerToken))
    {
        tokenValues.emplace_back(callerToken, ToVerbatimStringLiteral(GetCallerDisplayMethodName(pFrame)));
    }
    if (HasToken(b.logMessageParts, callstackToken))
    {
        tokenValues.emplace_back(callstackToken, ToVerbatimStringLiteral(GetCallStack(pThread)));
    }

    expandedLogMessageParts.reserve(b.logMessageParts.size());
    for (const auto &[text, isExpression] : b.logMessageParts)
    {
        expandedLogMessageParts.emplace_back(isExpression ? ExpandSpecialTokens(text, tokenValues) : text, isExpression);
    }
}

} // unnamed namespace

HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                           std::vector<uint32_t> &hitBreakpointIds)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> trFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&trFunctionBreakpoint)));

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    PDB::SequencePoint sp;
    PDB::GlobalFileIndex globalFileIndex;
    IfFailRet(DebugInfo::GetSequencePointByFrame(trFrame, sp, &globalFileIndex));

    const auto breakpoints = GetSourceResolvedBreakpoints().find(globalFileIndex);
    if (breakpoints == GetSourceResolvedBreakpoints().cend())
    {
        return E_FAIL;
    }

    auto &breakpointsInSource = breakpoints->second;
    const auto it = breakpointsInSource.find({sp.startLine, sp.startColumn});
    if (it == breakpointsInSource.cend())
    {
        return S_FALSE; // Stopped at a break, but no breakpoints.
    }

    std::list<ManagedSourceBreakpoint> &bList = it->second;
    if (bList.empty())
    {
        return S_FALSE; // Stopped at a break, but no breakpoints.
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));

    std::string sourceFilePath;
    std::string algorithm;
    std::string checksum;
    DebugInfo::GetSourceFile(globalFileIndex, sourceFilePath, algorithm, checksum);
    int32_t sourceReference = 0;
    SourceReference::GetSourceReference(globalFileIndex, sourceReference, sourceFilePath);

    // Only one source breakpoint is active per line:column pair; iterate the list to find all
    // matching active source breakpoints and add them to hitBreakpointIds.
    for (auto &b : bList)
    {
        for (auto &[trFuncBreakpoint, nativeAddress] : b.trFuncBreakpoints)
        {
            if (FAILED(Status = BreakpointHelpers::IsSameFunctionBreakpoint(trFunctionBreakpoint, trFuncBreakpoint)) ||
                Status == S_FALSE)
            {
                continue;
            }

            CORDB_ADDRESS currentNativeAddress = 0;
            if (SUCCEEDED(BreakpointHelpers::GetBreakpointNativeAddress(trFunctionBreakpoint, currentNativeAddress)) &&
                currentNativeAddress != nativeAddress)
            {
                nativeAddress = currentNativeAddress;
                Breakpoint breakpoint;
                b.ToBreakpoint(breakpoint, sourceFilePath, sourceReference, &algorithm, &checksum);
                breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                DAP::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
            }

            if (!b.condition.empty())
            {
                std::string output;
                if (FAILED(Status = BreakpointHelpers::IsEnableByCondition(pThread, b.condition, output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    b.ToBreakpoint(breakpoint, sourceFilePath, sourceReference, &algorithm, &checksum);
                    breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The condition for a breakpoint failed to evaluate and will be removed. The condition was '"
                    << b.condition << "'. The error returned was '" << output << "'. - "
                    << sourceFilePath << ':' << b.lineNum << ':' << b.columnNum << "\n";
                    breakpoint.message = ss.str();
                    DAP::EmitOutputEvent({OutputCategory::StdErr, breakpoint.message});
                    DAP::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
                    b.condition.clear();
                }
            }

            ++b.hitCount;

            if (!BreakpointHelpers::IsEnableByHitCondition(b.hitCount, b.hitCondition))
            {
                continue;
            }

            if (!b.logMessage.empty())
            {
                if (b.logMessageParts.empty())
                {
                    EvalTextHelpers::CreateTextWithEvalParts(b.logMessage, b.logMessageParts);
                    b.hasSpecialTokens = ExpressionHasSpecialTokens(b.logMessageParts);
                }

                std::string message;
                if (b.hasSpecialTokens)
                {
                    std::vector<std::pair<std::string, bool>> expandedLogMessageParts;
                    ExpandLogMessageTokens(b, pThread, trFrame, sourceFilePath, nativeAddress, expandedLogMessageParts);
                    EvalTextHelpers::BuildTextWithEval(pThread, nullptr, expandedLogMessageParts, message);
                }
                else
                {
                    EvalTextHelpers::BuildTextWithEval(pThread, nullptr, b.logMessageParts, message);
                }

                message += '\n';
                OutputEvent event(OutputCategory::Console, message);
                event.source = Source(sourceFilePath, sourceReference);
                if (!algorithm.empty() && !checksum.empty())
                {
                    event.source.checksums.emplace_back(algorithm, checksum);
                }
                event.line = b.lineNum;
                event.column = b.columnNum;
                DAP::EmitOutputEvent(event);
                continue;
            }

            hitBreakpointIds.emplace_back(b.id);
        }
    }

    return hitBreakpointIds.empty() ? S_FALSE : S_OK; // S_FALSE - stopped at a break, but the breakpoint was not found.
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    for (auto &[initialPathToSource, initialBreakpoints] : GetSourceBreakpointMapping())
    {
        for (auto &initialBreakpoint : initialBreakpoints)
        {
            if (initialBreakpoint.resolvedLineNum != 0)
            {
                continue;
            }

            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.lineNum = initialBreakpoint.breakpoint.line;
            bp.columnNum = initialBreakpoint.breakpoint.column;
            bp.endLine = initialBreakpoint.breakpoint.line;
            bp.endColumn = initialBreakpoint.breakpoint.column;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            bp.logMessage = initialBreakpoint.breakpoint.logMessage;
            PDB::GlobalFileIndex resolvedGlobalFileIndex;
            std::vector<PDB::ResolvedBreakpoint> resolvedPoints;
            Source source(initialPathToSource, initialBreakpoint.sourceReference);
            source.checksums = initialBreakpoint.checksums;

            if (FAILED(ResolveSourceBreakpoint(pModule, bp, source,
                                               resolvedPoints, resolvedGlobalFileIndex)) ||
                FAILED(ActivateSourceBreakpoint(bp, initialPathToSource, resolvedPoints)))
            {
                continue;
            }

            std::string resolvedPath;
            std::string algorithm;
            std::string checksum;
            DebugInfo::GetSourceFile(resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
            int32_t sourceReference = 0;
            SourceReference::GetSourceReference(resolvedGlobalFileIndex, sourceReference, resolvedPath);

            Breakpoint breakpoint;
            bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
            DAP::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});

            initialBreakpoint.resolvedGlobalFileIndex = resolvedGlobalFileIndex;
            initialBreakpoint.resolvedLineNum = bp.lineNum;
            initialBreakpoint.resolvedColumnNum = bp.columnNum;

            GetSourceResolvedBreakpoints()[resolvedGlobalFileIndex][{initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum}].push_back(std::move(bp));
        }
    }

    return S_OK;
}

HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::unordered_set<uint32_t> removedIds;

    for (auto fit = GetSourceResolvedBreakpoints().begin(); fit != GetSourceResolvedBreakpoints().end();)
    {
        std::unordered_map<std::pair<int32_t, int32_t>, std::list<ManagedSourceBreakpoint>, LineColumnHash> &fileResolvedBreakpoints = fit->second;

        for (auto lit = fileResolvedBreakpoints.begin(); lit != fileResolvedBreakpoints.end();)
        {
            std::list<ManagedSourceBreakpoint> &lineResolvedBreakpoints = lit->second;

            for (auto it = lineResolvedBreakpoints.begin(); it != lineResolvedBreakpoints.end();)
            {
                ManagedSourceBreakpoint &managedSourceBreakpoint = *it;

                assert(!managedSourceBreakpoint.trFuncBreakpoints.empty());

                CORDB_ADDRESS brModAddress = 0;
                // Check only the first element, see ActivateSourceBreakpoint() code:
                // the debugger doesn't support breakpoints with the same source name in different modules.
                if (FAILED(BreakpointHelpers::GetFunctionBreakpointModAddress(managedSourceBreakpoint.trFuncBreakpoints.at(0).first, brModAddress)) ||
                    modAddress != brModAddress)
                {
                    ++it;
                }
                else
                {
                    removedIds.emplace(managedSourceBreakpoint.id);
                    it = lineResolvedBreakpoints.erase(it);
                }
            }

            if (lineResolvedBreakpoints.empty())
            {
                lit = fileResolvedBreakpoints.erase(lit);
            }
            else
            {
                ++lit;
            }
        }

        if (fileResolvedBreakpoints.empty())
        {
            fit = GetSourceResolvedBreakpoints().erase(fit);
        }
        else
        {
            ++fit;
        }
    }

    // Reset removed resolved breakpoints.
    for (auto &sourceBreakpoints : GetSourceBreakpointMapping())
    {
        for (auto &bp : sourceBreakpoints.second)
        {
            if (removedIds.find(bp.id) != removedIds.cend())
            {
                bp.Reset();

                Breakpoint breakpoint;
                breakpoint.id = bp.id;
                breakpoint.verified = false;
                breakpoint.message = "Breakpoint reset at module unload.";
                DAP::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
            }
        }
    }

    return S_OK;
}

HRESULT SetSourceBreakpoints(bool haveProcess, const Source &source, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    const auto RemoveResolvedByInitialBreakpoint =
        [&](ManagedSourceBreakpointMapping &initialBreakpoint)
        {
            if (initialBreakpoint.resolvedLineNum == 0) // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()
            {
                return S_OK;
            }

            const auto bMap_it = GetSourceResolvedBreakpoints().find(initialBreakpoint.resolvedGlobalFileIndex);
            if (bMap_it == GetSourceResolvedBreakpoints().cend())
            {
                return E_FAIL;
            }

            const auto bList_it = bMap_it->second.find({initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum});
            if (bList_it == bMap_it->second.cend())
            {
                return E_FAIL;
            }

            for (auto itList = bList_it->second.begin(); itList != bList_it->second.end();)
            {
                if ((*itList).id == initialBreakpoint.id)
                {
                    itList = bList_it->second.erase(itList);
                    break;
                }
                else
                {
                    ++itList;
                }
            }

            if (bList_it->second.empty())
            {
                bMap_it->second.erase(bList_it);
            }

            return S_OK;
        };

    HRESULT Status = S_OK;
    if (sourceBreakpoints.empty())
    {
        const auto it = GetSourceBreakpointMapping().find(source.path);
        if (it != GetSourceBreakpointMapping().cend())
        {
            for (auto &initialBreakpoint : it->second)
            {
                Breakpoint breakpoint;
                breakpoint.id = initialBreakpoint.id;
                breakpoint.verified = initialBreakpoint.resolvedLineNum != 0;
                const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
                DAP::EmitBreakpointEvent(event);

                IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            }
            GetSourceBreakpointMapping().erase(it);
        }
        return S_OK;
    }

    auto &breakpointsInSource = GetSourceBreakpointMapping()[source.path];
    // Note, unlike before column support was added, an IDE may provide multiple breakpoints
    // on one line (with different columns). Key by the (line, column) pair to distinguish them.
    std::unordered_map<std::pair<int32_t, int32_t>, ManagedSourceBreakpointMapping *, LineColumnHash> breakpointsInSourceMap;

    // Remove old breakpoints
    std::unordered_set<std::pair<int32_t, int32_t>, LineColumnHash> funcBreakpointLinesColumns;
    for (const auto &sb : sourceBreakpoints)
    {
        funcBreakpointLinesColumns.emplace(sb.line, sb.column);
    }
    for (auto it = breakpointsInSource.begin(); it != breakpointsInSource.end();)
    {
        ManagedSourceBreakpointMapping &initialBreakpoint = *it;
        // Note, we don't remove the breakpoint in case `condition`, `hitCondition` or `logMessage` changed,
        // we only change these fields in the resolved breakpoint.
        if (funcBreakpointLinesColumns.find({initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column}) == funcBreakpointLinesColumns.cend())
        {
            Breakpoint breakpoint;
            breakpoint.id = initialBreakpoint.id;
            breakpoint.verified = initialBreakpoint.resolvedLineNum != 0;
            const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
            DAP::EmitBreakpointEvent(event);

            IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            it = breakpointsInSource.erase(it);
        }
        else
        {
            assert(breakpointsInSourceMap.find({initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column}) == breakpointsInSourceMap.cend());
            breakpointsInSourceMap.emplace(std::make_pair(initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column), &initialBreakpoint);
            ++it;
        }
    }

    // Note, DAP requires that "sourceBreakpoints" and "functionBreakpoints" must have the same indexes for the same breakpoints.

    for (const auto &sb : sourceBreakpoints)
    {
        const int32_t line = sb.line;
        const int32_t column = sb.column;
        Breakpoint breakpoint;

        const auto b = breakpointsInSourceMap.find({line, column});
        if (b == breakpointsInSourceMap.cend())
        {
            ManagedSourceBreakpointMapping initialBreakpoint;
            initialBreakpoint.breakpoint = sb;
            initialBreakpoint.id = getId();
            initialBreakpoint.sourceReference = source.sourceReference;
            initialBreakpoint.checksums = source.checksums;

            // New breakpoint
            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.lineNum = line;
            bp.columnNum = column;
            bp.endLine = line;
            bp.endColumn = column;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            bp.logMessage = initialBreakpoint.breakpoint.logMessage;
            PDB::GlobalFileIndex resolvedGlobalFileIndex;
            std::vector<PDB::ResolvedBreakpoint> resolvedPoints;

            if (haveProcess &&
                SUCCEEDED(ResolveSourceBreakpoint(nullptr, bp, source, resolvedPoints, resolvedGlobalFileIndex)) &&
                SUCCEEDED(ActivateSourceBreakpoint(bp, source.path, resolvedPoints)))
            {
                initialBreakpoint.resolvedGlobalFileIndex = resolvedGlobalFileIndex;
                initialBreakpoint.resolvedLineNum = bp.lineNum;
                initialBreakpoint.resolvedColumnNum = bp.columnNum;

                std::string resolvedPath;
                std::string algorithm;
                std::string checksum;
                DebugInfo::GetSourceFile(resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
                int32_t sourceReference = 0;
                SourceReference::GetSourceReference(resolvedGlobalFileIndex, sourceReference, resolvedPath);

                bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
                GetSourceResolvedBreakpoints()[resolvedGlobalFileIndex][{initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum}].push_back(std::move(bp));
            }
            else
            {
                bp.ToBreakpoint(breakpoint, source.path, source.sourceReference);
                if (!haveProcess)
                {
                    breakpoint.message = "The breakpoint is pending and will be resolved when debugging starts.";
                }
                else
                {
                    breakpoint.message = "The breakpoint will not currently be hit. No symbols have been loaded for this document.";
                }
            }

            breakpointsInSource.push_back(std::move(initialBreakpoint));
        }
        else
        {
            ManagedSourceBreakpointMapping &initialBreakpoint = *b->second;
            initialBreakpoint.breakpoint.condition = sb.condition;
            initialBreakpoint.breakpoint.hitCondition = sb.hitCondition;
            initialBreakpoint.breakpoint.logMessage = sb.logMessage;

            if (initialBreakpoint.resolvedLineNum != 0)
            {
                const auto bMap_it = GetSourceResolvedBreakpoints().find(initialBreakpoint.resolvedGlobalFileIndex);
                if (bMap_it == GetSourceResolvedBreakpoints().cend())
                {
                    return E_FAIL;
                }

                const auto bList_it = bMap_it->second.find({initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum});
                if (bList_it == bMap_it->second.cend())
                {
                    return E_FAIL;
                }

                for (auto &bp : bList_it->second)
                {
                    if (initialBreakpoint.id != bp.id)
                    {
                        continue;
                    }

                    // Existing breakpoint
                    const bool changedCondition = bp.condition != initialBreakpoint.breakpoint.condition;
                    const bool changedHitCondition = bp.hitCondition != initialBreakpoint.breakpoint.hitCondition;
                    const bool changedLogMessage = bp.logMessage != initialBreakpoint.breakpoint.logMessage;
                    bp.condition = initialBreakpoint.breakpoint.condition;
                    bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                    bp.logMessage = initialBreakpoint.breakpoint.logMessage;
                    if (changedLogMessage)
                    {
                        bp.logMessageParts.clear();
                        bp.hasSpecialTokens = false;
                    }
                    std::string resolvedPath;
                    std::string algorithm;
                    std::string checksum;
                    DebugInfo::GetSourceFile(initialBreakpoint.resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
                    int32_t sourceReference = 0;
                    SourceReference::GetSourceReference(initialBreakpoint.resolvedGlobalFileIndex, sourceReference, resolvedPath);

                    bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
                    if (changedCondition || changedHitCondition || changedLogMessage)
                    {
                        std::string changed;

                        if (changedCondition)
                        {
                            changed = "condition";
                        }
                        if (changedHitCondition)
                        {
                            if (changedCondition)
                            {
                                changed += ", ";
                            }
                            changed += "hitCondition";
                        }
                        if (changedLogMessage)
                        {
                            if (changedCondition || changedHitCondition)
                            {
                                changed += ", ";
                            }
                            changed += "logMessage";
                        }

                        breakpoint.message = "Breakpoint " + changed + " changed.";
                        const BreakpointEvent event(BreakpointEventReason::Changed, breakpoint);
                        DAP::EmitBreakpointEvent(event);
                        breakpoint.message.clear();
                    }
                    break;
                }
            }
            else
            {
                // Was already added, but was not yet resolved.
                ManagedSourceBreakpoint bp;
                bp.id = initialBreakpoint.id;
                bp.lineNum = line;
                bp.columnNum = column;
                bp.endLine = line;
                bp.endColumn = column;
                bp.condition = initialBreakpoint.breakpoint.condition;
                bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                bp.logMessage = initialBreakpoint.breakpoint.logMessage;
                bp.ToBreakpoint(breakpoint, source.path, source.sourceReference);
                if (!haveProcess)
                {
                    breakpoint.message = "The breakpoint is pending and will be resolved when debugging starts.";
                }
                else
                {
                    breakpoint.message = "The breakpoint will not currently be hit. No symbols have been loaded for this document.";
                }
            }
        }

        breakpoints.push_back(breakpoint);
    }

    return S_OK;
}

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    size_t count = 0;

    for (const auto &fileResolvedBreakpoints : GetSourceResolvedBreakpoints())
    {
        for (const auto &lineResolvedBreakpoints : fileResolvedBreakpoints.second)
        {
            for (const auto &managedSourceBreakpoint : lineResolvedBreakpoints.second)
            {
                count += managedSourceBreakpoint.trFuncBreakpoints.size();
            }
        }
    }

    return count;
}
#endif // DEBUG_INTERNAL_TESTS

void Initialize()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    GetSourceResolvedBreakpoints().clear();
    GetSourceBreakpointMapping().clear();
}

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    GetSourceResolvedBreakpoints().clear();
    // Reset the resolved breakpoints state.
    for (auto &sourceBreakpoints : GetSourceBreakpointMapping())
    {
        for (auto &bp : sourceBreakpoints.second)
        {
            bp.Reset();
        }
    }
}

} // namespace dncdbg::SourceBreakpoints
