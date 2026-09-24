// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/internal_helpers.h"
#include <fstream>
#include <iostream>
#include <mutex>

// for convenience
using nlohmann::json;

namespace dncdbg
{

void to_json(json &j, const Checksum &c)
{
    j = json{{"algorithm", c.algorithm},
             {"checksum", c.checksum}};
}

void to_json(json &j, const Source &s)
{
    j = json{{"name", s.name},
             {"path", s.path}};

    if (s.sourceReference >= 0)
    {
        j.emplace("sourceReference", s.sourceReference);
    }

    if (!s.checksums.empty())
    {
        j.emplace("checksums", s.checksums);
    }
}

void to_json(json &j, const Breakpoint &b)
{
    j = json{{"id",       b.id},
             {"verified", b.verified}};

    if (b.line != 0)
    {
        j.emplace("line", b.line);
    }

    if (b.column != 0)
    {
        j.emplace("column", b.column);
    }

    if (!b.message.empty())
    {
        j.emplace("message", b.message);
    }

    if (b.verified)
    {
        if (b.endLine != 0)
        {
            j.emplace("endLine", b.endLine);
        }
        if (b.endColumn != 0)
        {
            j.emplace("endColumn", b.endColumn);
        }
        if (!b.source.IsNull())
        {
            j.emplace("source", b.source);
        }
    }

    if (!b.instructionReference.empty())
    {
        j.emplace("instructionReference", b.instructionReference);
        j.emplace("offset", b.offset);
    }
}

void to_json(json &j, const StackFrame &f)
{
    j = json{{"id",        static_cast<int>(f.id)},
             {"name",      f.name},
             {"line",      f.line},
             {"column",    f.column},
             {"endLine",   f.endLine},
             {"endColumn", f.endColumn},
             {"moduleId",  f.moduleId}};
    if (!f.source.IsNull())
    {
        j.emplace("source", f.source);
    }
    if (!f.instructionPointerReference.empty())
    {
        j.emplace("instructionPointerReference", f.instructionPointerReference);
    }
    if (!f.presentationHint.empty())
    {
        j.emplace("presentationHint", f.presentationHint);
    }
}

void to_json(json &j, const Thread &t)
{
    j = json{{"id",   static_cast<int>(t.id)},
             {"name", t.name}};
}

void to_json(json &j, const Scope &s)
{
    j = json{{"name",               s.name},
             {"variablesReference", s.variablesReference},
             {"expensive",          s.expensive}};
}

void to_json(json &j, const Variable &v)
{
    j = json{{"name",               v.name},
             {"value",              v.value},
             {"type",               v.type},
             {"evaluateName",       v.evaluateName},
             {"variablesReference", v.variablesReference}};

    if (!v.memoryReference.empty())
    {
        j.emplace("memoryReference", v.memoryReference);
    }
}

void to_json(json &j, const Module &m)
{
    j = json{{"id",          m.id},
             {"name",        m.name},
             {"path",        m.path},
             {"isOptimized", m.isOptimized},
             {"isUserCode",  m.isUserCode}};

    if (!m.symbolFilePath.empty())
    {
        j.emplace("symbolFilePath", m.symbolFilePath);
    }

    if (!m.addressRange.empty())
    {
        j.emplace("addressRange", m.addressRange);
    }

    switch (m.symbolStatus)
    {
    case SymbolStatus::Skipped:
        j.emplace("symbolStatus", "Skipped loading symbols.");
        break;
    case SymbolStatus::Loaded:
        j.emplace("symbolStatus", "Symbols loaded.");
        break;
    case SymbolStatus::NotFound:
        j.emplace("symbolStatus", "Symbols not found.");
        break;
    }
}

void to_json(json &j, const GotoTarget &g)
{
    j = json{{"id",    g.id},
             {"label", g.label},
             {"line",  g.line}};

    if (g.column != 0)
    {
        j.emplace("column", g.column);
    }

    if (g.endLine != 0)
    {
        j.emplace("endLine", g.endLine);
    }

    if (g.endColumn != 0)
    {
        j.emplace("endColumn", g.endColumn);
    }

    if (!g.instructionPointerReference.empty())
    {
        j.emplace("instructionPointerReference", g.instructionPointerReference);
    }
}

void to_json(json &j, const BreakpointLocation &b)
{
    j = json{{"line", b.line}};

    if (b.column != 0)
    {
        j.emplace("column", b.column);
    }

    if (b.endLine != 0)
    {
        j.emplace("endLine", b.endLine);
    }

    if (b.endColumn != 0)
    {
        j.emplace("endColumn", b.endColumn);
    }
}

} // namespace dncdbg

namespace dncdbg::DAP
{

namespace
{

// Use a function-local static to avoid undefined behavior from a static std::ofstream
// member, whose constructor may throw.
std::ofstream &GetProtocolLog()
{
    static std::ofstream protocolLog;
    return protocolLog;
}

std::mutex &GetOutMutex()
{
    static std::mutex outMutex;
    return outMutex;
}

// Note: this counter must be protected by GetOutMutex().
uint64_t &GetSeqCounter()
{
    static uint64_t seqCounter = 1;
    return seqCounter;
}

// Caller must hold GetOutMutex().
void EmitMessage(nlohmann::json &message, std::string &output)
{
    message.emplace("seq", GetSeqCounter());
    ++GetSeqCounter();
    output = message.dump();
    std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
    std::cout.flush();
}

// Caller must hold GetOutMutex().
void LogInternal(std::string_view prefix, const std::string &text)
{
    if (!GetProtocolLog().is_open())
    {
        return;
    }

    GetProtocolLog() << prefix << text << std::endl; // NOLINT(performance-avoid-endl)
}

} // namespace

const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters()
{
    static const std::unordered_map<std::string, ExceptionBreakpointFilter> exceptionFilters{
        {"all", ExceptionBreakpointFilter::THROW},
        {"user-unhandled", ExceptionBreakpointFilter::USER_UNHANDLED}
    };
    return exceptionFilters;
}

void AddCapabilitiesTo(json &capabilities)
{
    capabilities.emplace("supportsConfigurationDoneRequest", true);
    capabilities.emplace("supportsFunctionBreakpoints", true);
    capabilities.emplace("supportsConditionalBreakpoints", true);
    capabilities.emplace("supportTerminateDebuggee", true);
    capabilities.emplace("supportsSetVariable", true);
    capabilities.emplace("supportsSetExpression", true);
    capabilities.emplace("supportsTerminateRequest", true);
    capabilities.emplace("supportsCancelRequest", true);
    capabilities.emplace("supportsExceptionInfoRequest", true);
    capabilities.emplace("supportsExceptionFilterOptions", true);
    json excFilters = json::array();
    for (const auto &entry : GetExceptionFilters())
    {
        const json filter{{"filter", entry.first},
                          {"label", entry.first}};
        excFilters.push_back(filter);
    }
    capabilities.emplace("exceptionBreakpointFilters", excFilters);
    capabilities.emplace("supportsExceptionOptions", false); // TODO add implementation
    capabilities.emplace("supportsHitConditionalBreakpoints", true);
    capabilities.emplace("supportsModulesRequest", true);
    capabilities.emplace("supportsLogPoints", true);
    capabilities.emplace("supportsGotoTargetsRequest", true);
    capabilities.emplace("supportsSingleThreadExecutionRequests", true);
    capabilities.emplace("supportsLoadedSourcesRequest", true);
    capabilities.emplace("supportsBreakpointLocationsRequest", true);
}

void SetupProtocolLoggingInternal(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    GetProtocolLog().open(path);
}

void EmitEvent(const std::string &name, const nlohmann::json &body)
{
    json message;
    message.emplace("type", "event");
    message.emplace("event", name);
    message.emplace("body", body);
    EmitMessageWithLog(LOG_EVENT, message);
}

void EmitMessageWithLog(std::string_view message_prefix, nlohmann::json &message)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    std::string output;
    EmitMessage(message, output);
    LogInternal(message_prefix, output);
}

void Log(std::string_view prefix, const std::string &text)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    LogInternal(prefix, text);
}

} // namespace dncdbg::DAP
