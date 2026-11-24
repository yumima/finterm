"""Asking a model, over the connection the app has for it.

A connection is what Settings holds now: {key, name, provider, model_id,
api_key, base_url, max_tokens}. It comes with every request and is never saved;
its key goes to its own endpoint and no other.
"""

from __future__ import annotations

import importlib.util
import sys
import threading
import time
from pathlib import Path

ANSWER_TIMEOUT_S = 300
_LOADING = threading.Lock()  # models are asked at the same time
_AGENTS = Path(__file__).resolve().parent.parent / "agents"


class NoAnswer(Exception):
    """The model gave none; the message says why."""


def _load(name: str, *where: str):
    """One of the agents' files, by path: "agents" is also a library's name."""
    with _LOADING:
        if name not in sys.modules:
            spec = importlib.util.spec_from_file_location(name, _AGENTS.joinpath(*where))
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module  # a dataclass looks its module up there
            try:
                spec.loader.exec_module(module)
            except BaseException:
                del sys.modules[name]
                raise
        return sys.modules[name]


def grounding():
    return _load("_arena_number_grounding", "finagent_core", "number_grounding.py")


def is_local(connection: dict) -> bool:
    """An engine on this machine: it needs no key, and runs one request at a time."""
    engines = _load("_arena_local_engines", "finagent_core", "registries", "local_engines.py")
    return engines.is_local_engine(connection.get("provider") or "", connection.get("base_url") or "")


def connection_for(model: dict, connections: list[dict]) -> dict:
    """The connection a competitor is asked over."""
    same = [c for c in connections if c.get("provider", "").lower() == model["provider"].lower()]
    by_key = [c for c in same if model.get("key") and c.get("key") == model["key"]]
    # A competition of before 4.1.1 knows its models by name only.
    found = by_key or [c for c in same if not model.get("key") and c.get("name") == model["name"]]
    if not found:
        raise NoAnswer(f"{model['name']} is not in Settings > LLM Config any more")
    return {**found[0], "model_id": model["model_id"]}


def ask(connection: dict, system: str, prompt: str, left_s: float = ANSWER_TIMEOUT_S) -> str:
    """The model's text. Raises NoAnswer. `left_s`: what is left of the cycle."""
    timeout = min(ANSWER_TIMEOUT_S, left_s)
    if timeout < 5:
        raise NoAnswer("there was no time left in this cycle")
    box: dict = {}

    def work():
        try:
            box["text"] = _ask(connection, system, prompt, timeout)
        except BaseException as e:  # it is told to the caller, whatever it was
            box["error"] = e

    # Not a pool: a thread still waiting for a model must not keep the process.
    thread = threading.Thread(target=work, daemon=True)
    thread.start()
    thread.join(timeout + 5)
    if thread.is_alive():
        raise NoAnswer(f"no answer after {timeout:.0f} s")
    if "error" in box:
        raise NoAnswer(_why(box["error"], connection)) from box["error"]
    return box["text"]


def _ask(connection: dict, system: str, prompt: str, timeout: float) -> str:
    from langchain_core.messages import HumanMessage, SystemMessage

    models = _load("_arena_llm_models", "deepagents", "models.py")
    until, local = time.monotonic() + timeout, is_local(connection)

    def invoke(**more):
        model = models.create_model({
            "llm_provider": connection.get("provider"), "llm_model": connection.get("model_id"),
            "llm_api_key": connection.get("api_key") or ("local" if local else ""),
            "llm_base_url": connection.get("base_url"), "llm_max_tokens": connection.get("max_tokens"),
            # What is left for both calls: an engine on this machine goes on with a request
            # nobody waits for until its connection is closed, and the next model waits for that.
            "llm_timeout_s": max(1.0, until - time.monotonic()), "llm_max_retries": 0 if local else 2,
        })
        if local and not connection.get("api_key"):
            # hearth adds a web search of its own to a chat that does not say no (as
            # LlmService does): one competitor would have what the others were not given.
            more["extra_body"] = {"web_search": False}
        return (model.bind(**more) if more else model).invoke([SystemMessage(content=system), HumanMessage(content=prompt)])

    answer = invoke()
    text = models.extract_text(answer.content).strip()
    cut_off = (answer.response_metadata or {}).get("finish_reason") == "length"
    if not text and cut_off and local and until - time.monotonic() > 5:
        # It spent its answer thinking; asked once more not to (Ollama's /v1 and hearth honour this).
        text = models.extract_text(invoke(reasoning_effort="none").content).strip()
    if not text and cut_off:
        raise NoAnswer("its answer was cut off: raise the profile's max tokens")
    if not text:
        raise NoAnswer("the model ended without an answer")
    return text


def _why(e: BaseException, connection: dict) -> str:
    """One line for the reader, without the key."""
    text = " ".join(str(e).split()) or type(e).__name__
    status = getattr(e, "status_code", None)
    if status in (401, 403):
        text = f"{connection.get('provider')} refused the API key ({status})"
    elif status == 429:
        text = f"{connection.get('provider')}'s rate limit or quota is used up (429)"
    elif any("Timeout" in c.__name__ for c in type(e).__mro__):
        text = "no answer in time"
    elif any(c.__name__ in ("APIConnectionError", "ConnectError") for c in type(e).__mro__):
        where = connection.get("base_url") or connection.get("provider")
        text = f"{where} could not be reached"
    key = str(connection.get("api_key") or "")
    if len(key) >= 6:
        text = text.replace(key, "…")
    return text[:300]
