// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/to_json.h"
#include "protocol/internal_helpers.h"

// for convenience
using nlohmann::json;

namespace dncdbg
{

// Note: objects are built with json::object() + emplace() instead of json{{key, value}, ...}.
// The braced form creates a temporary JSON array (heap allocation) for every key/value pair,
// which is significant for frequently serialized types (frames, variables, breakpoints).

void to_json(json &j, const Checksum &c)
{
    j = json::object();
    j.emplace("algorithm", c.algorithm);
    j.emplace("checksum", c.checksum);
}

void to_json(json &j, const Source &s)
{
    j = json::object();
    j.emplace("name", s.name);
    j.emplace("path", s.path);

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
    j = json::object();
    j.emplace("id", b.id);
    j.emplace("verified", b.verified);

    if (b.line != 0)
    {
        j.emplace("line", DAP::ToClientLine(b.line));
    }

    if (b.column != 0)
    {
        j.emplace("column", DAP::ToClientColumn(b.column));
    }

    if (!b.message.empty())
    {
        j.emplace("message", b.message);
    }

    if (b.verified)
    {
        if (b.endLine != 0)
        {
            j.emplace("endLine", DAP::ToClientLine(b.endLine));
        }
        if (b.endColumn != 0)
        {
            j.emplace("endColumn", DAP::ToClientColumn(b.endColumn));
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
    j = json::object();
    j.emplace("id", static_cast<int>(f.id));
    j.emplace("name", f.name);
    j.emplace("line", DAP::ToClientLine(f.line));
    j.emplace("column", DAP::ToClientColumn(f.column));
    j.emplace("endLine", DAP::ToClientLine(f.endLine));
    j.emplace("endColumn", DAP::ToClientColumn(f.endColumn));
    j.emplace("moduleId", f.moduleId);

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
    j = json::object();
    j.emplace("id", static_cast<int>(t.id));
    j.emplace("name", t.name);
}

void to_json(json &j, const Scope &s)
{
    j = json::object();
    j.emplace("name", s.name);
    j.emplace("variablesReference", s.variablesReference);
    j.emplace("expensive", s.expensive);
}

void to_json(json &j, const Variable &v)
{
    j = json::object();
    j.emplace("name", v.name);
    j.emplace("value", v.value);
    j.emplace("type", v.type);
    j.emplace("evaluateName", v.evaluateName);
    j.emplace("variablesReference", v.variablesReference);

    if (!v.memoryReference.empty())
    {
        j.emplace("memoryReference", v.memoryReference);
    }
}

void to_json(json &j, const Module &m)
{
    j = json::object();
    j.emplace("id", m.id);
    j.emplace("name", m.name);
    j.emplace("path", m.path);
    j.emplace("isOptimized", m.isOptimized);
    j.emplace("isUserCode", m.isUserCode);

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
    j = json::object();
    j.emplace("id", g.id);
    j.emplace("label", g.label);
    j.emplace("line", DAP::ToClientLine(g.line));

    if (g.column != 0)
    {
        j.emplace("column", DAP::ToClientColumn(g.column));
    }

    if (g.endLine != 0)
    {
        j.emplace("endLine", DAP::ToClientLine(g.endLine));
    }

    if (g.endColumn != 0)
    {
        j.emplace("endColumn", DAP::ToClientColumn(g.endColumn));
    }

    if (!g.instructionPointerReference.empty())
    {
        j.emplace("instructionPointerReference", g.instructionPointerReference);
    }
}

void to_json(json &j, const BreakpointLocation &b)
{
    j = json::object();
    j.emplace("line", DAP::ToClientLine(b.line));

    if (b.column != 0)
    {
        j.emplace("column", DAP::ToClientColumn(b.column));
    }

    if (b.endLine != 0)
    {
        j.emplace("endLine", DAP::ToClientLine(b.endLine));
    }

    if (b.endColumn != 0)
    {
        j.emplace("endColumn", DAP::ToClientColumn(b.endColumn));
    }
}

} // namespace dncdbg
