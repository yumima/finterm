#!/usr/bin/env python3
"""What finterm sends a local engine: never the engine's own web tools unasked.

hearth can add web search and page reading to a chat. A chat that reads the
user's portfolio must not get them unless the user turned them on, so every
OpenAI-shaped body says so: in C++ through LlmService::set_engine_fields, in
the agents through local_engines.NO_WEB_SEARCH. A new body that forgets is
what this test is for.
"""

import json
import os
import re
import sys
import types

APP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
        print(f"FAIL {name} {detail}")


# ── C++ ──────────────────────────────────────────────────────────────────────
raw = open(os.path.join(APP, "src", "ai_chat", "LlmService.cpp"), encoding="utf-8").read()
src = re.sub(r"(?<!:)//[^\n]*", "", raw)  # comments, not the // of a URL
functions = re.split(r"\n(?=[\w:<>*& ]+\bLlmService::\w+\()", src)
bodies = 0
for f in functions:
    name = re.search(r"LlmService::(\w+)\(", f)
    name = name.group(1) if name else "?"
    if "anthropic" in name or "ollama_native" in name:
        continue  # other protocols: hearth's web tools are on its OpenAI route only
    for m in re.finditer(r'(\w+)\["messages"\] = ', f):
        rest = f[m.end():]
        # the branch this body is written in: to its closing brace, or the function's end
        depth, end = 0, len(rest)
        for i, ch in enumerate(rest):
            depth += (ch == "{") - (ch == "}")
            if depth < 0:
                end = i
                break
        branch = rest[:end]
        if "anthropic" in f[:m.start()][-900:].lower() and "system" in branch and "set_engine_fields" not in branch:
            continue  # Anthropic's body: its system prompt is a field of its own
        bodies += 1
        check(f"{name}: {m.group(1)} says whether hearth may search the web",
              re.search(rf"set_engine_fields\({m.group(1)}, persona, ", branch))
check("the OpenAI-shaped bodies were found", bodies == 4, bodies)
fields = src[src.index("void LlmService::set_engine_fields"):]
check("only the user's setting turns the search on",
      'body["web_search"] = with_tools && web_search_;' in fields[:900])
check("...which is off unless it says on",
      'get(kWebSearchSetting, "false").value() == "true"' in src)
check("nothing else writes the field", src.count('"web_search"') == 1, src.count('"web_search"'))
check("no second default for the local engine", not re.search(r"(localhost|127\.0\.0\.1):\d+", src))

# ── Agents ───────────────────────────────────────────────────────────────────
AGENTS = os.path.join(APP, "scripts", "agents")
sys.path.insert(0, AGENTS)


def read(*p):
    return open(os.path.join(AGENTS, *p), encoding="utf-8").read()


from finagent_core.runtimes import local_runtime  # noqa: E402

body = lambda url, key="sk-no-key": json.loads(  # noqa: E731
    local_runtime._body(url, key, {"model": "m"}))
for url in ("http://127.0.0.1:11435/v1", "http://localhost:11435/v1", "http://[::1]:11435/v1"):
    check(f"the local runtime at {url}", body(url).get("web_search") is False)
check("a cloud endpoint is sent no field it may not know", body("https://api.example.com/v1") == {"model": "m"})
check("nor is a proxy with a key, on this machine", body("http://127.0.0.1:4000/v1", "sk-mine") == {"model": "m"})
os.environ.pop("FINTERM_LOCAL_BASE_URL", None)
check("its default engine is hearth",
      local_runtime._resolve_endpoint(None, None, None)[0] == "http://127.0.0.1:11435/v1")
runtime = read("finagent_core", "runtimes", "local_runtime.py")
check("both of its transports build their body there",
      runtime.count("body = _body(url, key, payload)") == runtime.count("/chat/completions\"") == 2)

# agno's OpenAIChat and langchain's ChatOpenAI, stood in for: what they are built with
seen = []


class Fake:
    def __init__(self, **kw):
        seen.append(kw)


class AnyModel(types.ModuleType):
    __path__ = []

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return Fake


for mod in ("agno", "agno.models", *(f"agno.models.{p}" for p in ("openai", "anthropic", "groq"))):
    sys.modules[mod] = AnyModel(mod)
for name in [n for n in os.environ if n.endswith("_API_KEY")]:
    del os.environ[name]  # this machine's keys are not the case's
from finagent_core.registries.models_registry import ModelsRegistry  # noqa: E402

NO = {"web_search": False}
LOCAL = "http://127.0.0.1:11435"
for provider, base, key, want in (
        ("hearth", "", "", NO), ("ollama", "", "", NO), ("hearth", LOCAL, "a-key", NO), ("openai", LOCAL + "/v1", "", NO),
        ("anthropic", LOCAL, "", NO),                # an SDK without a base_url, pointed at this machine
        ("openai", LOCAL + "/v1", "sk-mine", None),  # a proxy with a key
        ("groq", "https://g.example/v1", "k", None)):
    seen.clear()
    ModelsRegistry.create_model(provider, "primary_chat", base_url=base or None,
                                api_keys={f"{provider.upper()}_API_KEY": key} if key else {})
    check(f"registry: {provider} {base} {'with a key' if key else ''}",
          seen and seen[-1].get("extra_body") == want, seen)
factory = read("finagent_core", "agent_factory.py")
check("the agent factory's local engine", "extra_body=engine_fields(provider, base_url, api_key) or None" in factory)

m = types.ModuleType("langchain_openai")
m.ChatOpenAI = Fake
sys.modules["langchain_openai"] = m
sys.path.insert(0, os.path.join(AGENTS, "deepagents"))
import models as deep_models  # noqa: E402

P = lambda **k: {f"llm_{a}": b for a, b in k.items()}  # noqa: E731
seen.clear()
deep_models.create_model(P(provider="hearth", model="m", base_url="http://127.0.0.1:11435/v1"))
deep_models.create_model(P(provider="groq", model="m", api_key="k", base_url="https://g/v1"))
check("deep agent: hearth", seen[0].get("extra_body") == {"web_search": False}, seen)
check("deep agent: a cloud endpoint", "extra_body" not in seen[1], seen)

calls = []
lite = types.ModuleType("litellm")
lite.completion = lambda **kw: calls.append(("completion", kw))
lite.embedding = lambda **kw: calls.append(("embedding", kw))
sys.modules["litellm"] = lite
sys.path.insert(0, os.path.join(AGENTS, "rdagents"))
import llm as rd_llm  # noqa: E402

real_completion, real_embedding = lite.completion, lite.embedding
for provider, want in (("hearth", {"web_search": False}), ("groq", None)):
    calls.clear()
    lite.completion, lite.embedding = real_completion, real_embedding
    try:
        rd_llm.apply(P(provider=provider, model="m", api_key="k", base_url="http://h/v1"))
    except Exception:  # what follows the patch needs rdagent itself
        pass
    lite.completion(), lite.embedding()
    check(f"RD-Agent: {provider}", calls[0][1].get("extra_body") == want, calls)
    check(f"RD-Agent: {provider}'s embeddings are left alone", "extra_body" not in calls[1][1], calls)

print("ok" if not fails else f"{len(fails)} failed")
sys.exit(bool(fails))
