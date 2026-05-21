"""Tests for the McpService↔SDK tool bridge."""
from __future__ import annotations

from typing import Any

import pytest

# Skip the whole module when the SDK isn't installed — bridge needs
# `create_sdk_mcp_server` and `@tool` to build anything.
pytest.importorskip("claude_agent_sdk")

from finagent_core.runtimes.mcp_bridge import (
    EmptyToolBridge,
    ToolBridge,
    ToolDef,
    build_sdk_mcp_server,
)


class FakeToolBridge:
    """In-memory bridge with hard-coded tools for testing."""

    def __init__(self, tools: list[ToolDef], handlers: dict[str, Any]) -> None:
        self._tools = tools
        self._handlers = handlers
        self.invocations: list[tuple[str, dict]] = []

    def list_tools(self, agent_id: str) -> list[ToolDef]:  # noqa: ARG002
        return list(self._tools)

    async def invoke_tool(self, name: str, args: dict) -> Any:
        self.invocations.append((name, args))
        handler = self._handlers.get(name)
        if handler is None:
            raise KeyError(name)
        if callable(handler):
            return handler(args)
        return handler


def test_empty_bridge_yields_no_server() -> None:
    server = build_sdk_mcp_server(EmptyToolBridge(), agent_id="default")
    assert server is None


def test_empty_bridge_invoke_raises() -> None:
    import asyncio
    with pytest.raises(NotImplementedError, match="no tool bridge"):
        asyncio.run(EmptyToolBridge().invoke_tool("anything", {}))


def test_bridge_with_tools_builds_server() -> None:
    bridge = FakeToolBridge(
        tools=[
            ToolDef(
                name="int_markets_quote",
                description="Quote a ticker",
                input_schema={
                    "type": "object",
                    "properties": {"ticker": {"type": "string"}},
                    "required": ["ticker"],
                },
            ),
        ],
        handlers={"int_markets_quote": {"price": 100.0}},
    )
    server = build_sdk_mcp_server(bridge, agent_id="default")
    assert server is not None
    # McpSdkServerConfig is a TypedDict-like in newer SDK versions;
    # convert via dict() to inspect cleanly.
    cfg = dict(server)
    assert cfg.get("type") == "sdk"
    # The instance field carries the actual MCP server object that
    # holds the registered tool list — its presence is the contract.
    assert cfg.get("instance") is not None
    assert cfg.get("name") == "finterm"


def test_bridge_invocation_round_trip() -> None:
    """Bridge.invoke_tool round-trips name + args + result correctly."""
    import asyncio

    bridge = FakeToolBridge(
        tools=[
            ToolDef(name="echo", description="Echo", input_schema={}),
            ToolDef(name="quote", description="Quote", input_schema={}),
        ],
        handlers={
            "echo": lambda args: f"got {args.get('msg')}",
            "quote": lambda args: {"price": 100.0, "t": args.get("t")},
        },
    )

    # The SDK-server build is exercised separately
    # (test_bridge_with_tools_builds_server); here we just verify the
    # bridge half of the contract — given a name + args dict, the
    # bridge calls the handler and returns the result.
    assert asyncio.run(bridge.invoke_tool("echo", {"msg": "hi"})) == "got hi"
    assert asyncio.run(bridge.invoke_tool("quote", {"t": "AAPL"})) == {
        "price": 100.0, "t": "AAPL"
    }
    assert bridge.invocations == [
        ("echo", {"msg": "hi"}),
        ("quote", {"t": "AAPL"}),
    ]


def test_bridge_invocation_unknown_tool_raises() -> None:
    """An unknown name surfaces as a KeyError the SDK can render."""
    import asyncio

    bridge = FakeToolBridge(tools=[], handlers={})
    with pytest.raises(KeyError):
        asyncio.run(bridge.invoke_tool("nope", {}))


def test_anthropic_run_turn_accepts_tool_bridge(monkeypatch) -> None:
    """run_turn(fixture, tool_bridge=...) wires the bridge into ClaudeAgentOptions."""
    from pathlib import Path

    import claude_agent_sdk
    from claude_agent_sdk import ResultMessage

    from evals.fixture import load_fixture
    from finagent_core.runtimes import anthropic_runtime

    captured: dict = {}

    async def fake_query(*, prompt, options=None, transport=None):
        captured["options"] = options
        yield ResultMessage(
            subtype="success",
            duration_ms=1, duration_api_ms=1, is_error=False, num_turns=0,
            session_id="s", stop_reason="end_turn", total_cost_usd=0.0,
            usage=None, result="", structured_output=None, model_usage=None,
            permission_denials=None, deferred_tool_use=None, errors=None,
            api_error_status=None, uuid="r",
        )

    monkeypatch.setattr(claude_agent_sdk, "query", fake_query)
    import importlib
    importlib.reload(anthropic_runtime)

    hello_world = Path(__file__).resolve().parents[1] / "e2e" / "hello_world"
    fixture = load_fixture(hello_world)

    # Empty bridge → no mcp_servers in the options.
    anthropic_runtime.run_turn(fixture)
    assert "mcp_servers" not in (captured["options"].__dict__ or {}) or \
        not captured["options"].__dict__.get("mcp_servers")

    # Non-empty bridge → mcp_servers populated with our finterm server.
    bridge = FakeToolBridge(
        tools=[ToolDef(name="t1", description="t1", input_schema={})],
        handlers={"t1": "ok"},
    )
    anthropic_runtime.run_turn(fixture, tool_bridge=bridge)
    opts = captured["options"]
    assert hasattr(opts, "mcp_servers")
    servers = opts.mcp_servers
    assert isinstance(servers, dict)
    assert "finterm" in servers
