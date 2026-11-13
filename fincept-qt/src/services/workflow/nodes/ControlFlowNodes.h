#pragma once

namespace finterm::workflow {
class NodeRegistry;

/// Register control flow node types (IfElse, Switch, Loop, Split, Merge, Wait, etc.)
void register_control_flow_nodes(NodeRegistry& registry);

} // namespace finterm::workflow
