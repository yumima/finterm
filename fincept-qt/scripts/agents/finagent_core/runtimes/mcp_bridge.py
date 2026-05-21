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


@dataclass(frozen=True)
class ResourceDef:
    """One MCP resource's catalog metadata (no content — read via bridge)."""
    uri: str
    name: str
    description: str = ""
    mime_type: str = "application/json"


@dataclass(frozen=True)
class ResourceContentData:
    """Result of reading a resource through the bridge."""
    uri: str
    mime_type: str = "application/json"
    text: str = ""
    blob: bytes = b""
    error: str = ""


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

    def list_resources(self, agent_id: str) -> list[ResourceDef]:
        """Return the resource catalog visible to this agent.

        Defaults to empty when the bridge doesn't know about resources;
        production implementations call McpProvider::list_resources()
        plus the McpClientBase external aggregator.
        """
        return []

    async def read_resource(self, uri: str) -> ResourceContentData:
        """Read a resource by uri.

        Defaults to a missing-uri error; production wires through to
        McpProvider::read_resource() or the McpClientBase wire call.
        """
        return ResourceContentData(uri=uri, error=f"unknown resource: {uri}")


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

    def list_resources(self, agent_id: str) -> list[ResourceDef]:  # noqa: ARG002
        return []

    async def read_resource(self, uri: str) -> ResourceContentData:
        return ResourceContentData(uri=uri, error="no resource bridge configured")


def build_sdk_mcp_server(
    bridge: ToolBridge,
    agent_id: str,
    *,
    server_name: str = "finterm",
    server_version: str = "0.1.0",
) -> "McpSdkServerConfig | None":
    """Convert a ToolBridge into an in-process SDK MCP server.

    Returns None when the bridge has neither tools nor resources to
    serve — callers omit the `mcp_servers` entry in that case.

    After `create_sdk_mcp_server` returns its wrapper config, we reach
    into the underlying `mcp.server.lowlevel.Server` instance to add
    resource handlers (the SDK's convenience wrapper only takes
    `tools=` — resources are registered via the lowlevel decorator
    surface).

    Imports the SDK and underlying mcp types lazily so this module is
    importable without either installed.
    """
    from claude_agent_sdk import create_sdk_mcp_server, tool

    tool_defs = bridge.list_tools(agent_id)
    # ToolBridge is a structural Protocol — implementations may pre-date
    # the resources surface and not define list_resources/read_resource.
    # Treat absence as "no resources" rather than failing.
    list_resources_fn = getattr(bridge, "list_resources", None)
    resource_defs = list_resources_fn(agent_id) if list_resources_fn else []
    if not tool_defs and not resource_defs:
        return None

    sdk_tools = [_wrap_tool(td, bridge, tool) for td in tool_defs]
    config = create_sdk_mcp_server(
        name=server_name, version=server_version, tools=sdk_tools
    )

    if resource_defs:
        _attach_resources(config, resource_defs, bridge)

    return config


def _attach_resources(config, resource_defs: list[ResourceDef], bridge: ToolBridge) -> None:
    """Register list_resources + read_resource handlers on the SDK
    server's underlying mcp.server.lowlevel.Server instance.

    The SDK's `create_sdk_mcp_server(...)` returns an
    `McpSdkServerConfig` dict-like with `{type, name, instance}`;
    `.instance` is the lowlevel Server that supports
    `@server.list_resources()` and `@server.read_resource()`
    decorators.
    """
    from mcp import types as mcp_types
    from pydantic import AnyUrl

    mcp_server = dict(config).get("instance")
    if mcp_server is None:
        return

    # Pre-build the catalog of mcp_types.Resource objects.  Invalid
    # URIs would raise inside Resource(...); skip those silently
    # (catalog still works for the valid entries).
    catalog: list[mcp_types.Resource] = []
    for rd in resource_defs:
        try:
            catalog.append(mcp_types.Resource(
                name=rd.name,
                uri=AnyUrl(rd.uri),
                description=rd.description or None,
                mimeType=rd.mime_type or None,
            ))
        except Exception:  # noqa: BLE001 — bad URI; skip
            continue

    @mcp_server.list_resources()
    async def _list_resources():  # noqa: ARG001 — decorator signature
        return catalog

    # The lowlevel Server's @read_resource() decorator expects the
    # handler to return Iterable[ReadResourceContents] (helper_types),
    # not the typed TextResourceContents/BlobResourceContents — the
    # Server wraps the raw `.content` + `.mime_type` into the typed
    # contents itself.
    from mcp.server.lowlevel.helper_types import ReadResourceContents

    @mcp_server.read_resource()
    async def _read_resource(uri):
        content = await bridge.read_resource(str(uri))
        if content.error:
            # Raising here is the lowlevel-Server-friendly path; the
            # SDK envelope-wraps it into a JSON-RPC error for the model.
            raise RuntimeError(content.error)
        if content.blob:
            return [ReadResourceContents(
                content=content.blob,
                mime_type=content.mime_type or "application/octet-stream",
            )]
        return [ReadResourceContents(
            content=content.text,
            mime_type=content.mime_type or "text/plain",
        )]


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
