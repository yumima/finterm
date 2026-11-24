"""
llm.py — rdagent's model from a finterm LLM profile.

rdagent calls litellm, whose key normally comes from the environment. It must
not: the code rdagent has the model write runs as a child of this process and
inherits the environment. The key is handed to litellm's functions instead.
"""

from __future__ import annotations

import os
from typing import Any

_LOCAL = {"hearth", "ollama"}


def resolve(config: dict[str, Any]) -> tuple[str, str, str]:
    """(litellm model, key, api base) — ValueError names what is missing."""
    provider = str(config.get("llm_provider") or "").lower().strip()
    model = str(config.get("llm_model") or "").strip()
    key = str(config.get("llm_api_key") or "").strip()
    base = str(config.get("llm_base_url") or "").strip()
    if not provider or not model:
        raise ValueError("No LLM profile: choose one, or set one up in Settings > LLM Config")
    if not key and provider not in _LOCAL:
        raise ValueError(f"The LLM profile has no API key for {provider}")
    # A base is the profile's own endpoint, often a proxy with its own key: the
    # key must go nowhere else.
    if provider in ("anthropic", "claude"):
        return "anthropic/" + model, key, base.rstrip("/").removesuffix("/v1")  # litellm adds it
    if provider in ("google", "gemini") and not base:
        return "gemini/" + model, key, ""
    if not base:
        raise ValueError(f"The LLM profile for {provider} has no base URL")
    return "openai/" + model, key or "local", base  # any OpenAI-compatible endpoint


def embedding_model(config: dict[str, Any]) -> str:
    """The model the profile's endpoint embeds with, or "" if it is not known
    to: rdagent then runs without its knowledge base, which needs one."""
    provider = str(config.get("llm_provider") or "").lower().strip()
    if provider == "openai":
        return "openai/text-embedding-3-small"
    if provider in ("google", "gemini") and not config.get("llm_base_url"):
        return "gemini/text-embedding-004"
    if provider in _LOCAL:
        return "openai/nomic-embed-text"
    return ""


def apply(config: dict[str, Any]) -> None:
    """Before anything of rdagent is imported."""
    model, key, base = resolve(config)
    embed = embedding_model(config)
    os.environ["LITELLM_CHAT_MODEL"] = os.environ["CHAT_MODEL"] = model
    if embed:
        os.environ["LITELLM_EMBEDDING_MODEL"] = os.environ["EMBEDDING_MODEL"] = embed
    else:  # CoSTEER looks similar past tasks up by embedding
        for name in ("FACTOR_CoSTEER_", "MODEL_CoSTEER_", "CoSTEER_"):
            os.environ[name + "with_knowledge"] = "False"

    import functools
    import litellm
    common = {"api_key": key, **({"api_base": base} if base else {})}
    chat = dict(common)
    if str(config.get("llm_provider") or "").lower().strip() in _LOCAL:
        chat["extra_body"] = {"web_search": False}  # as finagent_core's local_engines.NO_WEB_SEARCH
    for name, fixed in (("completion", chat), ("embedding", common)):
        # rdagent does `from litellm import completion`: replaced before it does.
        setattr(litellm, name, functools.partial(getattr(litellm, name), **fixed))
