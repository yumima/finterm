"""local_runtime — OpenAI-compatible local runtime (Track 2).

Sibling of `anthropic_runtime.py`.  Talks to whatever OpenAI-compat
server the user pointed `local_llm_base_url` at — typically the
sibling local-AI-engine project, or directly to Ollama
(`http://localhost:11434/v1`), llama.cpp, vLLM, LM Studio, etc.

Shape mirrors anthropic_runtime exactly so the eval harness +
production callers (alpha_arena BaseAgent, AgentService consumers)
can swap runtimes by changing a single string:

    from finagent_core.runtimes.local_runtime import run_text
    text = run_text(prompt, system_prompt="…", base_url="http://localhost:11434/v1")

The richer `run_turn(fixture)` path (with tool-use loop, MCP
bridge, skill loading) is a Track 9 follow-up — needs an
OpenAI-compat tool-calling loop that's compatible with the local
server's function-calling support (Ollama tools, llama.cpp tools).
For now the local path supports plain chat completions; tool
calls go through the Anthropic path until that loop lands.

No silent failover.  When the local server is unreachable the
caller gets `LocalRuntimeUnavailable` — the chat surface surfaces
a banner with a link to the sibling-project setup docs (per the
design doc R-A3 acceptance criterion).
"""
from __future__ import annotations

import json
import logging
import os
from typing import Any, Callable
from urllib import error, request

logger = logging.getLogger(__name__)


class LocalRuntimeUnavailable(RuntimeError):
    """Raised when the local OpenAI-compat server can't be reached or
    returns an error.  The caller surfaces this verbatim; we never
    auto-fall-back to Anthropic (R5)."""


# Default base URL — Ollama's OpenAI-compat endpoint.  Override via
# the FINCEPT_LOCAL_BASE_URL env var or by passing `base_url=` to
# run_text().  Production reads it from LlmConfigRepository on the
# C++ side and passes it through.
DEFAULT_BASE_URL = "http://localhost:11434/v1"

# Reasonable default model identifier — most local stacks ship a
# model with this exact tag.  Caller usually overrides.
DEFAULT_MODEL = "qwen2.5:14b-instruct"


def _resolve_endpoint(base_url: str | None, model: str | None, api_key: str | None) -> tuple[str, str, str]:
    """Resolve URL / model / key with env-var fallbacks.  Centralised so
    run_text and run_with_tools agree on the precedence (caller-supplied
    kwarg > env var > module default)."""
    url = (base_url or os.environ.get("FINCEPT_LOCAL_BASE_URL") or DEFAULT_BASE_URL).rstrip("/")
    model_id = model or os.environ.get("FINCEPT_LOCAL_MODEL") or DEFAULT_MODEL
    key = api_key or os.environ.get("FINCEPT_LOCAL_API_KEY") or "sk-no-key"
    return url, model_id, key


def _post_chat_completions(url: str, key: str, payload: dict, timeout_s: float) -> dict:
    """POST a single `chat/completions` request and parse the response.
    Wraps every transport / JSON failure into `LocalRuntimeUnavailable`
    so the caller's error path is uniform."""
    body = json.dumps(payload).encode("utf-8")
    req = request.Request(
        f"{url}/chat/completions",
        data=body,
        headers={
            "Content-Type": "application/json",
            # Some servers (Ollama) ignore Authorization; others (vLLM
            # behind an API gateway) require it.  Passed key wins over
            # env var so concurrent callers don't race on global state.
            "Authorization": f"Bearer {key}",
        },
        method="POST",
    )
    try:
        with request.urlopen(req, timeout=timeout_s) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except error.URLError as exc:
        raise LocalRuntimeUnavailable(
            f"local LLM server not reachable at {url}: {exc.reason}"
        ) from exc
    except (TimeoutError, OSError) as exc:
        raise LocalRuntimeUnavailable(
            f"local LLM server timed out at {url}: {exc}"
        ) from exc
    except json.JSONDecodeError as exc:
        raise LocalRuntimeUnavailable(
            f"local LLM server returned non-JSON at {url}: {exc}"
        ) from exc


def _first_message(url: str, data: dict) -> dict:
    """Extract `choices[0].message` defensively.  Servers vary in what
    they return when no choices come back — surface that as
    `LocalRuntimeUnavailable` rather than crashing on `[0]`."""
    choices = data.get("choices") or []
    if not choices:
        raise LocalRuntimeUnavailable(
            f"local LLM server returned no choices at {url}: {json.dumps(data)[:200]}"
        )
    first = choices[0]
    if not isinstance(first, dict):
        raise LocalRuntimeUnavailable(
            f"local LLM server returned non-object choice at {url}"
        )
    msg = first.get("message")
    if isinstance(msg, dict):
        return msg
    # Legacy "text completion" shape — wrap as a message-ish dict.
    text = first.get("text")
    if isinstance(text, str):
        return {"role": "assistant", "content": text}
    return {"role": "assistant", "content": str(first)}


def run_text(
    prompt: str,
    *,
    system_prompt: str | None = None,
    base_url: str | None = None,
    model: str | None = None,
    api_key: str | None = None,
    max_tokens: int = 1024,
    temperature: float = 0.7,
    timeout_s: float = 60.0,
) -> str:
    """Production-shaped entry point: text-in / text-out.

    Equivalent to anthropic_runtime.run_text — same return shape, same
    failure mode (`*Unavailable` exception).  Calling this when the
    local server is down raises `LocalRuntimeUnavailable`, never a
    silent fallback.

    Parameters mirror the Anthropic side where they apply.  `max_tokens`
    + `temperature` are passed through to the OpenAI-compat endpoint.
    """
    url, model_id, key = _resolve_endpoint(base_url, model, api_key)

    messages: list[dict[str, str]] = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": prompt})

    data = _post_chat_completions(url, key, {
        "model": model_id,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": False,
    }, timeout_s)
    msg = _first_message(url, data)
    content = msg.get("content")
    return content if isinstance(content, str) else ""


def run_with_tools(
    prompt: str,
    *,
    tools: list[dict],
    tool_dispatcher: Callable[[str, dict[str, Any]], Any],
    system_prompt: str | None = None,
    base_url: str | None = None,
    model: str | None = None,
    api_key: str | None = None,
    max_tokens: int = 1024,
    temperature: float = 0.7,
    timeout_s: float = 60.0,
    max_turns: int = 10,
) -> str:
    """Run a tool-using conversation against the local server.

    `tools` follows the OpenAI tools-schema shape — list of
    `{"type": "function", "function": {"name", "description",
    "parameters": {...JSON Schema...}}}`.  Most local servers that
    support tool-calling (Ollama 0.4+, vLLM, llama.cpp via
    OpenAI-shim, LM Studio, Groq, …) accept this verbatim.

    `tool_dispatcher(tool_name, args) -> Any` is called for every
    `tool_call` the model returns.  Whatever it returns is JSON-encoded
    (or `str()`-ified) into the matching `tool_result` message.  In
    finterm, the dispatcher is `mcp_bridge.invoke_tool(...)`.

    Loop:
      1. POST current messages + tools.
      2. If response has tool_calls → run each, append tool_result
         messages, GOTO 1.
      3. Otherwise → return the assistant content.

    Loops at most `max_turns` rounds — guards against a model that
    won't stop calling tools.  When the cap is hit raises
    `LocalRuntimeUnavailable` so the caller knows the turn didn't
    finish; the trace records partial state.

    Same auth / endpoint / failure-mode contract as `run_text`.
    """
    url, model_id, key = _resolve_endpoint(base_url, model, api_key)

    messages: list[dict[str, Any]] = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": prompt})

    for turn in range(max_turns):
        data = _post_chat_completions(url, key, {
            "model": model_id,
            "messages": messages,
            "tools": tools,
            "max_tokens": max_tokens,
            "temperature": temperature,
            "stream": False,
        }, timeout_s)
        msg = _first_message(url, data)
        tool_calls = msg.get("tool_calls") if isinstance(msg, dict) else None

        if not tool_calls:
            content = msg.get("content")
            return content if isinstance(content, str) else ""

        # Append the assistant turn (verbatim — keep tool_calls so the
        # server's bookkeeping lines up) before each tool_result.
        messages.append(msg)

        for tc in tool_calls:
            tc_id = tc.get("id") or ""
            fn = tc.get("function") or {}
            name = fn.get("name") or ""
            raw_args = fn.get("arguments")
            try:
                args = json.loads(raw_args) if isinstance(raw_args, str) else (raw_args or {})
            except json.JSONDecodeError:
                args = {}

            # Dispatcher exceptions become tool errors instead of
            # bubbling up — same posture the SDK takes.  The model
            # gets a chance to recover.
            try:
                result = tool_dispatcher(name, args if isinstance(args, dict) else {})
            except Exception as exc:  # noqa: BLE001
                result = {"error": f"{type(exc).__name__}: {exc}"}

            messages.append({
                "role": "tool",
                "tool_call_id": tc_id,
                "name": name,
                "content": (
                    json.dumps(result, default=str)
                    if isinstance(result, (dict, list)) else str(result)
                ),
            })

    raise LocalRuntimeUnavailable(
        f"local runtime tool loop exceeded max_turns={max_turns} at {url}"
    )


def probe(base_url: str | None = None, timeout_s: float = 5.0) -> list[str]:
    """List models available at the local server.  Used by the chat
    UI's "Probe" button (Settings → LLM Config) and by the
    LocalRuntimeUnavailable banner's diagnostics.

    Raises LocalRuntimeUnavailable on connection failure so the caller
    can surface a clear error — no empty-list-equals-success ambiguity.
    """
    url = (base_url or os.environ.get("FINCEPT_LOCAL_BASE_URL") or DEFAULT_BASE_URL).rstrip("/")
    req = request.Request(
        f"{url}/models",
        headers={
            "Authorization": f"Bearer {os.environ.get('FINCEPT_LOCAL_API_KEY', 'sk-no-key')}",
        },
    )
    try:
        with request.urlopen(req, timeout=timeout_s) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except error.URLError as exc:
        raise LocalRuntimeUnavailable(
            f"local LLM server not reachable at {url}: {exc.reason}"
        ) from exc
    except (TimeoutError, OSError, json.JSONDecodeError) as exc:
        raise LocalRuntimeUnavailable(
            f"local LLM server error at {url}: {exc}"
        ) from exc

    names: list[str] = []
    for item in data.get("data", []) or []:
        if isinstance(item, dict) and isinstance(item.get("id"), str):
            names.append(item["id"])
    return sorted(names)
