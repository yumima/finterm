"""Tests for resource support in the McpService↔SDK bridge (Track 5 commit E)."""
from __future__ import annotations

from typing import Any

import pytest

pytest.importorskip("claude_agent_sdk")

from finagent_core.runtimes.mcp_bridge import (
    EmptyToolBridge,
    ResourceContentData,
    ResourceDef,
    ToolDef,
    build_sdk_mcp_server,
)


class FakeBridge:
    """Bridge with hard-coded tools + resources for tests."""

    def __init__(
        self,
        tools: list[ToolDef] | None = None,
        resources: list[ResourceDef] | None = None,
        resource_payloads: dict[str, ResourceContentData] | None = None,
    ) -> None:
        self._tools = tools or []
        self._resources = resources or []
        self._payloads = resource_payloads or {}
        self.invocations: list[tuple[str, Any]] = []

    def list_tools(self, agent_id: str) -> list[ToolDef]:  # noqa: ARG002
        return list(self._tools)

    async def invoke_tool(self, name: str, args: dict) -> Any:  # noqa: ARG002
        self.invocations.append(("tool", name))
        return "ok"

    def list_resources(self, agent_id: str) -> list[ResourceDef]:  # noqa: ARG002
        return list(self._resources)

    async def read_resource(self, uri: str) -> ResourceContentData:
        self.invocations.append(("resource", uri))
        if uri in self._payloads:
            return self._payloads[uri]
        return ResourceContentData(uri=uri, error=f"unknown: {uri}")


def test_empty_bridge_yields_no_server() -> None:
    """No tools AND no resources → no server."""
    assert build_sdk_mcp_server(EmptyToolBridge(), agent_id="default") is None


def test_resources_only_bridge_still_yields_server() -> None:
    """Resources without tools is a valid server (resources surface only)."""
    bridge = FakeBridge(resources=[
        ResourceDef(uri="finterm://portfolio/snapshot", name="Portfolio"),
    ])
    server = build_sdk_mcp_server(bridge, agent_id="default")
    assert server is not None


def test_resources_attached_to_sdk_server() -> None:
    """Registered resources are reachable via the lowlevel Server's
    list_resources handler."""
    import asyncio

    bridge = FakeBridge(
        resources=[
            ResourceDef(
                uri="finterm://portfolio/snapshot",
                name="Portfolio snapshot",
                description="Holdings",
                mime_type="application/json",
            ),
            ResourceDef(
                uri="finterm://watchlist/all",
                name="Watchlists",
                mime_type="application/json",
            ),
        ],
    )
    server = build_sdk_mcp_server(bridge, agent_id="default")
    assert server is not None

    mcp_server = dict(server)["instance"]

    # The lowlevel Server uses decorators that register handlers in
    # request_handlers; invoke the list_resources handler directly.
    from mcp import types as mcp_types
    handler = mcp_server.request_handlers.get(mcp_types.ListResourcesRequest)
    assert handler is not None, "list_resources handler not registered"

    # Build a ListResourcesRequest envelope and invoke.
    req = mcp_types.ListResourcesRequest(method="resources/list", params=None)
    result = asyncio.run(handler(req))
    assert hasattr(result, "root") or hasattr(result, "resources")
    # The handler returns ServerResult-wrapped ListResourcesResult.
    listed = result.root if hasattr(result, "root") else result
    uris = {str(r.uri) for r in listed.resources}
    # AnyUrl renders with a trailing slash for path-less URIs; compare prefixes.
    assert any("portfolio/snapshot" in u for u in uris)
    assert any("watchlist/all" in u for u in uris)


def test_read_resource_routes_through_bridge() -> None:
    """The SDK server's read_resource handler invokes bridge.read_resource."""
    import asyncio

    bridge = FakeBridge(
        resources=[ResourceDef(uri="finterm://portfolio/snapshot", name="x")],
        resource_payloads={
            "finterm://portfolio/snapshot": ResourceContentData(
                uri="finterm://portfolio/snapshot",
                mime_type="application/json",
                text='{"holdings":[]}',
            ),
        },
    )
    server = build_sdk_mcp_server(bridge, agent_id="default")
    mcp_server = dict(server)["instance"]

    from mcp import types as mcp_types
    from pydantic import AnyUrl
    handler = mcp_server.request_handlers.get(mcp_types.ReadResourceRequest)
    assert handler is not None

    req = mcp_types.ReadResourceRequest(
        method="resources/read",
        params=mcp_types.ReadResourceRequestParams(
            uri=AnyUrl("finterm://portfolio/snapshot"),
        ),
    )
    result = asyncio.run(handler(req))
    # bridge saw the read
    assert ("resource", "finterm://portfolio/snapshot") in bridge.invocations
    # the response carries the text payload
    contents = result.root.contents if hasattr(result, "root") else result.contents
    assert len(contents) == 1
    assert "holdings" in contents[0].text


def test_read_resource_error_bubbles_as_runtime_error() -> None:
    """Bridge errors raise RuntimeError so the SDK envelope-wraps them."""
    import asyncio

    bridge = FakeBridge(
        resources=[ResourceDef(uri="finterm://missing", name="m")],
        # no payload → bridge default returns error
    )
    server = build_sdk_mcp_server(bridge, agent_id="default")
    mcp_server = dict(server)["instance"]

    from mcp import types as mcp_types
    from pydantic import AnyUrl
    handler = mcp_server.request_handlers.get(mcp_types.ReadResourceRequest)
    req = mcp_types.ReadResourceRequest(
        method="resources/read",
        params=mcp_types.ReadResourceRequestParams(uri=AnyUrl("finterm://missing")),
    )
    with pytest.raises(RuntimeError, match="unknown"):
        asyncio.run(handler(req))


def test_empty_resource_bridge_methods_have_defaults() -> None:
    """EmptyToolBridge satisfies the resource half of the ToolBridge Protocol."""
    import asyncio

    eb = EmptyToolBridge()
    assert eb.list_resources("default") == []
    rc = asyncio.run(eb.read_resource("finterm://anything"))
    assert rc.error
    assert "no resource bridge" in rc.error
