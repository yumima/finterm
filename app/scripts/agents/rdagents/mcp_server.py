"""
mcp_server.py — finterm's own read-only tools, over MCP, for a client on this
computer (Claude Desktop, an editor).

    mcp_server.py '{"terminal_mcp_endpoint", "terminal_tools", "port"}'

The tools and their answers are the app's: quotes, fundamentals, news,
portfolios. Every call goes to the app over its loopback bridge, so there is
nothing of finterm's data in this file to go stale. The address ends in a
random path, new at every start and shown only in the app: other accounts and
web pages on this computer can reach the port, and portfolios are private.

Prints {"success", "url", "tools"} once it listens; runs until the app ends it.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import secrets
import socket
import sys
from pathlib import Path
from typing import Any

logging.basicConfig(stream=sys.stderr, level=logging.WARNING)
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "finagent_core" / "tools"))

from terminal_toolkit import TerminalToolkit  # noqa: E402

DEFAULT_PORT = 18765


def build_app(toolkit: TerminalToolkit, path: str) -> Any:
    from mcp import types
    from mcp.server.lowlevel import Server
    from mcp.server.streamable_http_manager import StreamableHTTPSessionManager
    from mcp.server.transport_security import TransportSecuritySettings
    from starlette.applications import Starlette
    from starlette.routing import Route

    server: Any = Server("finterm")
    defs = {d["name"]: d for d in toolkit.tool_definitions if d.get("name")}

    @server.list_tools()
    async def list_tools() -> list[types.Tool]:
        return [types.Tool(name=n, description=d.get("description") or n,
                           inputSchema=d.get("inputSchema") or {"type": "object", "properties": {}})
                for n, d in defs.items()]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict[str, Any]) -> list[types.TextContent]:
        if name not in defs:
            raise ValueError(f"no such tool: {name}")
        out = await asyncio.to_thread(toolkit._call_tool, name, arguments or {})
        return [types.TextContent(type="text", text=out)]

    manager = StreamableHTTPSessionManager(
        app=server, stateless=True, json_response=True,
        security_settings=TransportSecuritySettings(
            enable_dns_rebinding_protection=True,
            allowed_hosts=["127.0.0.1:*", "localhost:*"], allowed_origins=["http://127.0.0.1:*", "http://localhost:*"]))

    @contextlib.asynccontextmanager
    async def lifespan(_app: Any):
        async with manager.run():
            yield

    class Endpoint:  # an ASGI app of its own: a Mount would redirect before the checks above
        async def __call__(self, scope: Any, receive: Any, send: Any) -> None:
            await manager.handle_request(scope, receive, send)

    return Starlette(routes=[Route(path, endpoint=Endpoint())], lifespan=lifespan)


def listen(port: int) -> socket.socket:
    """On the asked port, else any free one; bound here so that the address
    given out is the one that is served."""
    for p in (port, 0):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if sys.platform != "win32":
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("127.0.0.1", p))
            s.listen(64)
            return s
        except OSError:
            s.close()
    raise OSError("no free port")


def main() -> None:
    try:
        params = json.loads(sys.argv[1]) if len(sys.argv) > 1 else {}
        import mcp  # noqa: F401
        import uvicorn
        defs = params.get("terminal_tools") or []
        if not params.get("terminal_mcp_endpoint") or not defs:
            raise RuntimeError("the app offered no tools")
        toolkit = TerminalToolkit(params["terminal_mcp_endpoint"], defs)
        path = f"/{secrets.token_urlsafe(24)}/mcp"
        app = build_app(toolkit, path)
        sock = listen(int(params.get("port") or DEFAULT_PORT))
    except Exception as e:  # noqa: BLE001 — the app shows the reason
        print(json.dumps({"success": False, "error": f"{type(e).__name__}: {e}"}), flush=True)
        sys.exit(1)

    port = sock.getsockname()[1]

    class Announcing(uvicorn.Server):
        async def startup(self, sockets: Any = None) -> None:
            await super().startup(sockets=sockets)
            print(json.dumps({"success": True, "url": f"http://127.0.0.1:{port}{path}", "port": port,
                              "tools": [d["name"] for d in defs]}), flush=True)

    # No access log: the address is the key.
    asyncio.run(Announcing(uvicorn.Config(app, log_level="warning", access_log=False)).serve(sockets=[sock]))


if __name__ == "__main__":
    main()
