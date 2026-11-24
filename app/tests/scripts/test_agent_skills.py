"""What an agent is given: its skills' text (the user's copy first), and an
answer rather than an empty one from a model that only thought."""
import http.server
import json
import os
import sys
import tempfile
import threading
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
tmp = tempfile.TemporaryDirectory()
os.environ["FINTERM_USER_DIR"] = tmp.name
sys.path.insert(0, str(APP / "scripts" / "agents" / "finagent_core" / "runtimes"))
import importlib.util  # noqa: E402


def load(name):
    spec = importlib.util.spec_from_file_location(name, APP / "scripts/agents/finagent_core/runtimes" / f"{name}.py")
    m = importlib.util.module_from_spec(spec)
    sys.modules[name] = m
    spec.loader.exec_module(m)
    return m


fails = 0


def check(name, ok, got=""):
    global fails
    if not ok:
        fails += 1
        print(f"FAIL - {name}: {got}")


S = load("skills_loader")
text = S.skills_text(["morning-note", "no-such-skill", "morning-note"])
check("the skill's text, once, without its front matter", text.count("<skill name=") == 1 and "pre-market note" in text
      and "description:" not in text, text[:200])
check("a name with no skill is dropped", "no-such" not in text)
check("no skills, no text", S.skills_text([]) == "" and S.skills_text(None) == "" and S.skills_text(["no-such-skill"]) == "")
mine = Path(tmp.name) / "skills" / "equity-research" / "morning-note"
mine.mkdir(parents=True)
(mine / "SKILL.md").write_text("---\nname: morning-note\n---\nMY OWN WAY\n", encoding="utf-8")
check("the user's copy wins", "MY OWN WAY" in S.skills_text(["morning-note"]) and "pre-market note" not in S.skills_text(["morning-note"]))
own = Path(tmp.name) / "skills" / "mine" / "my-skill"
own.mkdir(parents=True)
(own / "SKILL.md").write_text("no front matter", encoding="utf-8")
check("a skill of the user's own", "no front matter" in S.skills_text(["my-skill"]) and "my-skill" in S.list_available_skills())
src = (APP / "scripts/agents/finagent_core/core_agent.py").read_text(encoding="utf-8")
check("agents are built with it", 'skills_text(config.get("skills"))' in src)
check("a seeded agent's skills reach its config",
      'out[QStringLiteral("skills")]' in (APP / "src/services/agents/AgentService.cpp").read_text(encoding="utf-8"))

L = load("local_runtime")
seen = []


class H(http.server.BaseHTTPRequestHandler):
    """A model that thinks its budget away unless told not to."""
    stubborn = False

    def do_POST(self):
        b = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        seen.append(b)
        quiet = b.get("reasoning_effort") == "none" and not H.stubborn
        msg = {"role": "assistant", "content": "391" if quiet else ""}
        fin = "stop" if quiet else "length"
        if b.get("stream"):
            out = (f'data: {json.dumps({"choices": [{"delta": {"reasoning": "hm"}, "finish_reason": fin}]})}\n\n'
                   "data: [DONE]\n\n").encode()
        else:
            out = json.dumps({"choices": [{"message": msg, "finish_reason": fin}]}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{srv.server_port}/v1"
kw = {"base_url": url, "model": "m"}
check("asked again without thinking", L.run_text("17*23?", **kw) == "391" and len(seen) == 2 and "reasoning_effort" not in seen[0], seen)
check("the same streaming", "".join(L.run_text_stream("17*23?", **kw)) == "391")
tools = {"tools": [], "tool_dispatcher": lambda n, a: {}}
check("and with tools", L.run_with_tools("17*23?", **tools, **kw) == "391")
ev = list(L.run_with_tools_stream("17*23?", **tools, **kw))
check("and with tools, streaming", [e.get("text") for e in ev if e["type"] == "content"] == ["391"], ev)
H.stubborn = True
try:
    r = L.run_text("17*23?", **kw)
    check("no answer is said, not returned as an empty one", False, repr(r))
except L.LocalRuntimeUnavailable as e:
    check("no answer is said, not returned as an empty one", "without an answer" in str(e), e)

print("agent_skills:", "ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
