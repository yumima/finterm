#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace fincept::mcp::tools {
std::vector<ToolDef> get_notes_tools();
std::vector<Resource> get_notes_resources();
std::vector<Prompt> get_notes_prompts();
} // namespace fincept::mcp::tools
