#pragma once
// ArtefactTools.h — `emit_artefact` MCP tool (Track 5 / artefact system).
//
// Agents call this tool to publish a typed structured output —
// comps table, DCF model, IC memo, report — to the chat_artefacts
// store instead of dumping into a chat bubble.  The artefact then
// surfaces in the Workbench Artefacts panel: re-runnable, exportable,
// auditable.
//
// Caller responsibility: pass `_meta.request_id` in args (or rely on
// ToolContext to forward it).  The tool stitches together the
// artefact's source provenance so re-runs can dispatch with the same
// agent + skill + args without the user retyping anything.

#include "mcp/McpTypes.h"

#include <vector>

namespace fincept::mcp::tools {

std::vector<ToolDef> get_artefact_tools();

} // namespace fincept::mcp::tools
