"""Deep Agents (scripts/agents/deepagents): what it is allowed, what it answers
with, and what it remembers. No model, no network."""
import json
import os
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace as NS

APP = Path(__file__).resolve().parents[2]
tmp = tempfile.TemporaryDirectory()
os.environ["FINTERM_USER_DIR"] = tmp.name
os.environ["FINTERM_DATA_DIR"] = tmp.name
sys.path.insert(0, str(APP / "scripts"))
sys.path.insert(0, str(APP / "scripts" / "agents" / "deepagents"))
import agent  # noqa: E402
import cli  # noqa: E402
import models  # noqa: E402

fails = 0


def check(name, ok, got=""):
    global fails
    if not ok:
        fails += 1
        print(f"FAIL - {name}: {got}")


# The model's endpoint and what a profile may lack.
for cfg, word in (({}, "profile"), ({"llm_provider": "openai", "llm_model": "m"}, "API key"),
                  ({"llm_provider": "groq", "llm_model": "m", "llm_api_key": "k"}, "base URL")):
    try:
        models.create_model(cfg)
        check(f"refused: {cfg}", False)
    except ValueError as e:
        check(f"says what is missing: {word}", word in str(e), e)
check("text blocks become one string",
      models._plain({"role": "system", "content": [{"type": "text", "text": "a"}, {"type": "text", "text": "b"}]})["content"] == "a\n\nb")
img = {"role": "user", "content": [{"type": "text", "text": "a"}, {"type": "image_url", "image_url": {}}]}
check("a message with an image is left alone", models._plain(img) is img)
check("thinking is not text", models.extract_text([{"type": "thinking", "thinking": "x"}, {"type": "text", "text": "y"}]) == "y")

# No disk, no shell: nothing but state, whatever the caller's config says.
src = "\n".join((APP / "scripts" / "agents" / "deepagents" / f).read_text(encoding="utf-8")
                for f in ("agent.py", "cli.py", "models.py", "subagents.py"))
for word in ("LocalShellBackend", "FilesystemBackend", "CompositeBackend", "backend_mode"):
    check(f"no {word}", word not in src)
check("no tools offered, none built", agent.build_tools({}, agent.ToolLog()) == [])

# The answer.
ai = lambda text, calls=(): NS(type="ai", content=text, tool_calls=list(calls))  # noqa: E731
state = {"messages": [NS(type="human", content="q"), ai("See r.md."), ],
         "files": {"/r.md": {"content": ["# Report", "body"]}, "/notes.md": "a sub-agent's",
                   "/large_tool_results/r.md": "DUMP", "/conversation_history/r.md": "HISTORY"}}
text = cli.answer_text(state)
check("the last message, and the file it names", text.startswith("See r.md.") and "# Report\nbody" in text, text)
check("not the library's files nor another's", not any(w in text for w in ("DUMP", "HISTORY", "sub-agent")), text)
wrote = NS(type="ai", content="", tool_calls=[{"name": "write_file", "args": {"file_path": "/final_report.md"}}])
text = cli.answer_text({"messages": [wrote, ai("The full report is saved.")], "files": {"/final_report.md": "THE REPORT", "/notes.md": "other"}})
check("the report it wrote, though it does not name it", "THE REPORT" in text and "other" not in text, text)
check("a plan to use a tool is no answer",
      cli.answer_text({"messages": [ai("Let me fetch.", ["c"]), NS(type="tool", content="x"), ai("")]}) == ""
      and cli.answer_text({"messages": [ai("Let me fetch.", ["c"])]}) == "")

for bad in ("../x", "a/b", ".hidden", "x" * 65):
    r = cli.cmd_execute_task({"task": "t", "thread_id": bad})
    check(f"thread id {bad[:8]!r} refused", not r["success"] and "thread ID" in r["error"], r)
check("an unknown agent type is refused", not cli.cmd_execute_task({"task": "t", "agent_type": "zzz"})["success"])
check("no profile is said", "profile" in cli.cmd_execute_task({"task": "t"})["error"].lower())

# A run: figures traced to the tools, the thread carried on.
asked = []


class FakeAgent:
    def __init__(self, log):
        self.log = log

    def invoke(self, st, config=None):
        asked.append(st["messages"])
        self.log.append(json.dumps({"price": 187.43}))
        return {"messages": [NS(type="tool", content="sub-agent says revenue is 999.5 billion"),
                             NS(type="ai", content="AAPL is at 187.43. Revenue will be 999.5 billion.")]}


agent.build_tools = lambda cfg, log: setattr(agent, "_log", log) or []
agent.create_agent = lambda model, kind, tools: FakeAgent(agent._log)
cli.create_model = lambda cfg: object()
r = cli.cmd_execute_task({"task": "price?", "thread_id": "t1"})
check("it answers", r["success"], r)
check("a tool's figure stays", "187.43" in r["result"], r["result"])
check("a figure only a sub-agent wrote does not", "999.5" not in r["result"], r["result"])
cli.cmd_execute_task({"task": "and then?", "thread_id": "t1"})
check("the thread's earlier turn goes with the next task",
      [m[0] for m in asked[-1]] == ["user", "assistant", "user"] and asked[-1][0][1] == "price?", asked[-1])
check("another thread starts empty", cli.cmd_execute_task({"task": "x"})["success"] and len(asked[-1]) == 1)

print("deep_agent:", "ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
