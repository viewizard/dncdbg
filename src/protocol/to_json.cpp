// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/to_json.h"

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
