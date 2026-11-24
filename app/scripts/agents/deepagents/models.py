"""
models.py — the LangChain model for a finterm LLM profile.

The app resolves the endpoint (LlmService::api_root: the profile's own base, or
the provider's), so here there are two native clients and one for every
OpenAI-compatible endpoint: OpenAI, OpenRouter, Groq, DeepSeek, xAI, Kimi,
MiniMax — and hearth or Ollama on this machine.
"""

from __future__ import annotations

from typing import Any

_LOCAL = {"hearth", "ollama"}


def create_model(config: dict[str, Any]):
    """
    Config keys: llm_provider, llm_model, llm_api_key, llm_base_url.

    Raises ValueError naming what the profile lacks.
    """
    provider = str(config.get("llm_provider") or "").lower().strip()
    model = str(config.get("llm_model") or "").strip()
    api_key = str(config.get("llm_api_key") or "").strip()
    base_url = str(config.get("llm_base_url") or "").strip()

    if not provider or not model:
        raise ValueError("No LLM profile: choose one, or set one up in Settings > LLM Config")
    if not api_key and provider not in _LOCAL:
        raise ValueError(f"The LLM profile has no API key for {provider}")

    if provider in ("anthropic", "claude"):
        from langchain_anthropic import ChatAnthropic
        kwargs: dict[str, Any] = {"api_key": api_key, "model_name": model}
        if base_url:
            kwargs["base_url"] = base_url.rstrip("/").removesuffix("/v1")  # the SDK adds it
        return ChatAnthropic(**kwargs)

    if provider in ("google", "gemini"):
        from langchain_google_genai import ChatGoogleGenerativeAI
        return ChatGoogleGenerativeAI(google_api_key=api_key, model=model)

    if not base_url:
        raise ValueError(f"The LLM profile for {provider} has no base URL")
    from langchain_openai import ChatOpenAI

    class PlainTextChat(ChatOpenAI):
        def _get_request_payload(self, *args: Any, **kwargs: Any) -> dict:
            payload = super()._get_request_payload(*args, **kwargs)
            payload["messages"] = [_plain(m) for m in payload.get("messages", [])]
            return payload

    # Chat completions: the one API every compatible server has.
    return PlainTextChat(api_key=api_key or "local", model=model, base_url=base_url, use_responses_api=False)


def _plain(message: dict[str, Any]) -> dict[str, Any]:
    """A message of text blocks as one string. deepagents writes its system
    prompt as blocks, and local servers take only a string there (hearth's
    Ollama answered 500)."""
    content = message.get("content")
    if not isinstance(content, list) or not all(isinstance(b, dict) and b.get("type") == "text" for b in content):
        return message
    return {**message, "content": "\n\n".join(b.get("text", "") for b in content)}


def extract_text(content: Any) -> str:
    """The text of a model's message: a string, or a list of content blocks
    (whose thinking and tool-use blocks are not text)."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return " ".join(b if isinstance(b, str) else b.get("text", "") for b in content
                        if isinstance(b, str) or (isinstance(b, dict) and b.get("type") == "text"))
    return "" if content is None else str(content)
