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
from typing import Any
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
    url = (base_url or os.environ.get("FINCEPT_LOCAL_BASE_URL") or DEFAULT_BASE_URL).rstrip("/")
    model_id = model or os.environ.get("FINCEPT_LOCAL_MODEL") or DEFAULT_MODEL
    key = api_key or os.environ.get("FINCEPT_LOCAL_API_KEY") or "sk-no-key"

    messages: list[dict[str, str]] = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": prompt})

    payload = {
        "model": model_id,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": False,
    }
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
            data = json.loads(resp.read().decode("utf-8"))
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

    # Standard OpenAI response: choices[0].message.content.  Some
    # servers return choices[0].text (legacy completion shape) — fall
    # back to that, then to repr() so we surface *something* in the
    # trace instead of silently dropping.
    choices = data.get("choices") or []
    if not choices:
        raise LocalRuntimeUnavailable(
            f"local LLM server returned no choices at {url}: {json.dumps(data)[:200]}"
        )
    first = choices[0]
    if isinstance(first, dict):
        msg = first.get("message") or {}
        text = msg.get("content") if isinstance(msg, dict) else None
        if isinstance(text, str):
            return text
        text = first.get("text")
        if isinstance(text, str):
            return text
    return str(first)


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
