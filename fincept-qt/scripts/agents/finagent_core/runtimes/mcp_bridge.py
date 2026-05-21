"""ToolBridge — exposes finterm's McpService tools to the Anthropic SDK.

The C++ `McpService` owns the 31 internal tool families plus any
external stdio MCP servers registered through the McpManager.  The
Anthropic-path runtime (claude-agent-sdk in Python) needs that same
catalog to be reachable from the agent loop.

Rather than spinning up an MCP-over-stdio server inside finterm
(explicit non-goal per §1), we register an **in-process** SDK MCP
server (`create_sdk_mcp_server`) populated with Python tool stubs
that proxy invocations through a `ToolBridge`.  When the bridge is
backed by the real AgentService↔finagent_core IPC (out of Track 3
item 11 scope), tool calls flow:

   Claude → SDK → in-process tool handler → ToolBridge.invoke_tool
        → IPC out → McpService → tool family → result → IPC back

For Track 3 item 11 we ship:
  - ToolBridge protocol (list_tools + invoke_tool)
  - EmptyToolBridge (default; eval-harness uses this — no tools)
  - build_sdk_mcp_server() factory that converts a ToolBridge's
    catalog into an McpSdkServerConfig the SDK can consume

The actual McpService IPC implementation lands later when
AgentService wires its end of the bridge — at that point a real
ToolBridge implementation is plugged in via the
`tool_bridge` kwarg on `anthropic_runtime.run_turn(...)`.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Protocol, runtime_checkable, TYPE_CHECKING

if TYPE_CHECKING:
    from claude_agent_sdk import McpSdkServerConfig


@dataclass(frozen=True)
class ToolDef:
    """One tool's metadata, matching the MCP tool-definition shape.

    `name` is the source-prefixed identifier (e.g. `int:portfolio.peek`,
    `fd:get_income_statements`, `ext:fetch.url`) once Track 8 lands —
    until then bare names are accepted.
    """
    name: str
    description: str
    input_schema: dict[str, Any] = field(default_factory=dict)


@runtime_checkable
class ToolBridge(Protocol):
    """Catalog + invocation surface between the SDK and McpService."""

    def list_tools(self, agent_id: str) -> list[ToolDef]:
        """Return the tool catalog visible to this agent.

        Respects per-agent allowlists (R7) — implementations call
        through to `McpService::list_tools_for(agent_id)` and convert.
        """

    async def invoke_tool(self, name: str, args: dict[str, Any]) -> Any:
        """Execute a tool by name, return its result.

        Raises if the tool isn't in the catalog for this agent or the
        bridge cannot reach McpService.
        """


class EmptyToolBridge:
    """Default bridge — empty catalog, raises on any invocation.

    Used by the eval harness in fixtures that don't expect tool calls
    and by Track 3 item 11 itself before the real McpService IPC is
    wired.  Tests inject their own ToolBridge via run_turn(
    tool_bridge=...) to exercise specific tool flows.
    """

    def list_tools(self, agent_id: str) -> list[ToolDef]:  # noqa: ARG002
        return []

    async def invoke_tool(self, name: str, args: dict[str, Any]) -> Any:  # noqa: ARG002
        raise NotImplementedError(
            f"no tool bridge configured — call to {name!r} ignored"
        )


def build_sdk_mcp_server(
    bridge: ToolBridge,
    agent_id: str,
    *,
    server_name: str = "finterm",
    server_version: str = "0.1.0",
) -> "McpSdkServerConfig | None":
    """Convert a ToolBridge into an in-process SDK MCP server.

    Returns None when the bridge's catalog for `agent_id` is empty —
    callers omit the `mcp_servers` entry in that case.

    Imports the SDK lazily so this module is importable without
    claude-agent-sdk installed (matches the eval-harness pattern).
    """
    from claude_agent_sdk import create_sdk_mcp_server, tool

    tool_defs = bridge.list_tools(agent_id)
    if not tool_defs:
        return None

    sdk_tools = [_wrap_tool(td, bridge, tool) for td in tool_defs]
    return create_sdk_mcp_server(
        name=server_name, version=server_version, tools=sdk_tools
    )


def _wrap_tool(tool_def: ToolDef, bridge: ToolBridge, sdk_tool_decorator):
    """Wrap one ToolDef as an SDK-decorated async handler.

    Each handler closes over the bridge so an invocation routes back
    to McpService via the same path other tools use.
    """
    async def handler(args: dict[str, Any]) -> dict[str, Any]:
        try:
            result = await bridge.invoke_tool(tool_def.name, args)
        except Exception as exc:  # noqa: BLE001 — return the error to the model, don't crash the SDK
            return {
                "content": [
                    {"type": "text", "text": f"tool error: {type(exc).__name__}: {exc}"}
                ],
                "isError": True,
            }
        return {
            "content": [{"type": "text", "text": _stringify(result)}],
        }

    return sdk_tool_decorator(tool_def.name, tool_def.description, tool_def.input_schema or {})(handler)


def _stringify(result: Any) -> str:
    """Coerce a tool result into a string the model can consume.

    Tool result shapes vary across families — McpService returns dicts
    for typed payloads and plain strings for free-text.  The SDK content
    block accepts a string, so we coerce here; structured callers will
    parse the text back into JSON if needed.
    """
    if isinstance(result, str):
        return result
    if result is None:
        return ""
    try:
        import json
        return json.dumps(result, default=str)
    except (TypeError, ValueError):
        return str(result)
