#pragma once
#include "mcp/McpTypes.h"

#include <vector>

// FinancialDatasetsTools — wraps the financialdatasets.ai REST API
// surface that upstream's MCP server doesn't expose (section-level
// SEC filings, insider trades, 13-F holdings, segmented financials,
// operational KPIs, earnings).  See plans/ai-stack-free-local.md
// §7.2 + Track 6.
//
// API key configuration:
//   SecureStorage id: "mcp.tools.financial-datasets"
//   Source: financialdatasets.ai free-tier signup
//   The tools fail with a clear error when the key isn't configured.

namespace fincept::mcp::tools {
std::vector<ToolDef> get_financial_datasets_tools();
} // namespace fincept::mcp::tools
