"""Local OpenAI-compatible engines — one rule for every place a model is built.

finterm's own engine is the hearth gateway (OpenAI-compatible, by default
http://127.0.0.1:11435). Settings saves it as provider "hearth" (or "ollama",
which is routed to hearth too). It speaks the OpenAI protocol but needs no API
key — yet the OpenAI client refuses to start without one ("OPENAI_API_KEY not
set. Please set the OPENAI_API_KEY environment variable.").

A local engine is a known local provider, or ANY provider whose base_url is on
this machine (loopback) — it gets a placeholder key when it has none. Its API
root: hearth / ollama, and a bare host:port, get the /v1 root (the SDK appends
/chat/completions itself); a loopback URL with its own path (Open WebUI's
/api, a proxy's /openai) is the user's root and is used as given.
"""

import ipaddress
from urllib.parse import urlparse

DEFAULT_LOCAL_BASE_URL = "http://127.0.0.1:11435"
PLACEHOLDER_KEY = "sk-no-key"  # local engines ignore the key; the client needs one
LOCAL_PROVIDERS = {"hearth", "ollama"}

# For a local engine's request body. A hearth from before 2026-11-24 adds its
# own web search and page reading to any chat that does not say no; an agent
# works with the user's data, and a page it reads could have it send that out.
# finterm's agents have their own tools.
NO_WEB_SEARCH = {"web_search": False}


def engine_fields(provider: str, base_url: str = "", api_key: str = "") -> dict:
    """What a request's body must also say (LlmService::set_engine_fields' rule):
    to a local engine, no web search. An endpoint with a key of its own, on this
    machine or not, may be a proxy to a cloud that refuses a field it does not
    know, so it is sent none."""
    keyless = not api_key or api_key == PLACEHOLDER_KEY
    if (provider or "").lower() in LOCAL_PROVIDERS or (keyless and is_local_engine(provider, base_url)):
        return dict(NO_WEB_SEARCH)
    return {}


def _parse(base_url: str):
    url = (base_url or "").strip()
    if url and "//" not in url:
        url = "http://" + url  # "localhost:1234/v1" has no hostname without a scheme
    try:
        return urlparse(url)
    except ValueError:
        return None


def _is_loopback(host: str) -> bool:
    if not host:
        return False
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def is_local_engine(provider: str, base_url: str = "") -> bool:
    if (provider or "").lower() in LOCAL_PROVIDERS:
        return True
    parsed = _parse(base_url)
    return bool(parsed) and _is_loopback(parsed.hostname or "")


def local_base_url(provider: str, base_url: str = "") -> str:
    """The engine's OpenAI root (see the module note)."""
    if (provider or "").lower() in LOCAL_PROVIDERS or not base_url:
        parsed = _parse(base_url or DEFAULT_LOCAL_BASE_URL)
    else:
        parsed = _parse(base_url)
        if parsed and parsed.path.strip("/") not in ("", "v1"):
            return parsed.geturl().rstrip("/")  # the user's own root
    base = parsed.geturl().rstrip("/") if parsed else DEFAULT_LOCAL_BASE_URL
    if base.endswith("/v1"):
        base = base[:-3]
    return base + "/v1"
