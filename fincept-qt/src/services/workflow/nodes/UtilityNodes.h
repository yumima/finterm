#pragma once

namespace finterm::workflow {
class NodeRegistry;

/// Register utility node types (HttpRequest, Code, Set, DateTime, etc.)
void register_utility_nodes(NodeRegistry& registry);

} // namespace finterm::workflow
