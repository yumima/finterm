"""RD-Agent jobs: what starts, what is recorded, how a job that died is told
apart. rdagent itself is stood in for; no model, no Docker, no network."""
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace as NS

APP = Path(__file__).resolve().parents[2]
RD = APP / "scripts" / "agents" / "rdagents"
tmp = tempfile.TemporaryDirectory()
os.environ["FINTERM_USER_DIR"] = os.environ["FINTERM_DATA_DIR"] = tmp.name
sys.path.insert(0, str(RD))
import cli  # noqa: E402
import jobs  # noqa: E402
import llm  # noqa: E402
import worker  # noqa: E402

fails = 0


def check(name, ok, got=""):
    global fails
    if not ok:
        fails += 1
        print(f"FAIL - {name}: {got}")


# The model: an endpoint of litellm's own, or any OpenAI-compatible one.
L = lambda **k: {f"llm_{a}": b for a, b in k.items()}  # noqa: E731
check("anthropic", llm.resolve(L(provider="anthropic", model="m", api_key="k", base_url="https://api.anthropic.com/v1"))[0::2] == ("anthropic/m", ""))
check("groq", llm.resolve(L(provider="groq", model="m", api_key="k", base_url="https://g/v1")) == ("openai/m", "k", "https://g/v1"))
check("a local one needs no key", llm.resolve(L(provider="hearth", model="m", base_url="http://h/v1"))[1] == "local")
for c in ({}, L(provider="openai", model="m"), L(provider="groq", model="m", api_key="k")):
    try:
        llm.resolve(c)
        check(f"refused {c}", False)
    except ValueError:
        pass
src = (RD / "llm.py").read_text()
check("the key is not put in the environment", "API_KEY" not in src and 'os.environ["OPENAI' not in src)

check("the goal says what rdagent has no setting for",
      "0.04" in worker.goal_text("factor_mining", {"task_description": "x", "target_ic": 0.04})
      and "max drawdown" in worker.goal_text("model_optimization", {"model_type": "gru", "optimization_target": "max_drawdown"}))

# What it found.
exp = NS(result={"IC": 0.031, "1day.excess_return_with_cost.information_ratio": 1.2},
         sub_tasks=[NS(factor_name="mom", factor_description="momentum", name="mom", description="momentum")],
         sub_workspace_list=[NS(all_codes="print(1)")])
bad = NS(result=None, sub_tasks=[NS(name="net", architecture="gru", description="d")], sub_workspace_list=[None])
res = worker.results_of([(exp, NS(decision=True, reason="good")), (bad, NS(decision=False, reason="failed"))])
check("metrics of the iteration's backtest", res[0]["metrics"]["ic"] == 0.031 and res[0]["metrics"]["information_ratio"] == 1.2, res[0])
check("no result is no metric, not zero", res[1]["metrics"]["ic"] is None and worker.best_ic(res) == 0.031, res[1])
check("factor or model", [r["items"][0]["kind"] for r in res] == ["factor", "model"])

# Task IDs are folder names.
for t in ("../x", "a/b", "", "a.b"):
    try:
        jobs.folder(t)
        check(f"task id {t!r} refused", False)
    except ValueError:
        pass

# A whole job in a process of its own, with a stand-in loop.
DRIVER = f"""
import sys, time
sys.path.insert(0, {str(RD)!r})
import worker, llm
from types import SimpleNamespace as NS
worker.missing = lambda c: []
llm.apply = lambda c: None
class Loop:
    def __init__(self):
        self.trace = NS(hist=[]); self.loop_trace = {{0: [1]}}; self.steps = ["propose", "coding"]
    async def run(self, loop_n):
        print("loop says hello")
        for i in range(loop_n):
            self.trace.hist.append((NS(result={{"IC": 0.01 * (i + 1)}}, sub_tasks=[NS(name="f%d" % i, description="")],
                                       sub_workspace_list=[]), NS(decision=True, reason="")))
        mode = sys.argv[3]
        if mode == "fail": raise RuntimeError("no backtest")
        if mode == "hang": time.sleep(60)
worker.make_loop = lambda job: Loop()
worker.main()
"""
drv = Path(tmp.name) / "drv.py"
drv.write_text(DRIVER)
KEY = "sk-must-not-be-written"


def start(mode, **params):
    p = subprocess.Popen([sys.executable, str(drv), "start", json.dumps({"type": "factor_mining", "max_iterations": 2,
                          "task_description": "g", "config": {"llm_api_key": KEY, "llm_model": "m"}, **params}), mode],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=tmp.name)
    return p, json.loads(p.stdout.readline())


p, a = start("ok")
check("it says it started", a["success"] and a["status"] == "started", a)
out, err = p.communicate(timeout=60)
job = jobs.load(a["task_id"])
check("a job that ends is completed", job["status"] == "completed" and job["iterations_completed"] == 2 and job["best_ic"] == 0.02, job)
check("what rdagent prints is in its log, not in the answer", out == b"" and "hello" in (jobs.folder(a["task_id"]) / "worker.log").read_text(), out)
check("the key is nowhere in its folder", not any(KEY in f.read_text(errors="replace") for f in jobs.folder(a["task_id"]).rglob("*") if f.is_file()))
conda = jobs.folder(a["task_id"]) / "bin" / "conda"
if os.name != "nt":
    r = subprocess.run([str(conda), "run", "-n", "finterm", "--no-capture-output", "echo", "hi"], capture_output=True, text=True)
    check("the stand-in for conda runs the command", r.stdout == "hi\n", r)
    check("...and nothing else", subprocess.run([str(conda), "create", "-y"], capture_output=True).returncode != 0)
r = cli.cmd_get_discovered_factors({"task_id": a["task_id"]})
check("its factors are read back", [f["name"] for f in r["factors"]] == ["f0", "f1"] and r["best_ic"] == 0.02, r)

p, a = start("fail")
p.communicate(timeout=60)
job = jobs.load(a["task_id"])
check("a failure is one, with its reason", job["status"] == "failed" and "no backtest" in job["error"], job)

if os.name != "nt":
    for how, want in (("terminate", "stopped"), ("kill", "interrupted")):
        p, a = start("hang")
        time.sleep(1.5)
        check("it runs", jobs.load(a["task_id"])["status"] == "running", jobs.load(a["task_id"]))
        r = subprocess.run([sys.executable, str(drv), "resume", json.dumps({"task_id": a["task_id"]}), "ok"], capture_output=True, cwd=tmp.name)
        check("it can't be run twice", b"already running" in r.stdout, r.stdout)
        getattr(p, how)()
        p.communicate(timeout=60)
        check(f"after {how}: {want}", jobs.load(a["task_id"])["status"] == want, jobs.load(a["task_id"]))
check("listed newest first, filtered by status", len(cli.cmd_list_tasks({})["tasks"]) >= 2
      and all(t["status"] == "failed" for t in cli.cmd_list_tasks({"status": "failed"})["tasks"]))

# A start that can't: the reasons, and nothing on disk.
n = len(list(jobs.workspace_base().iterdir()))
r = subprocess.run([sys.executable, str(RD / "worker.py"), "start", json.dumps({"type": "factor_mining", "config": {}})],
                   capture_output=True, text=True, timeout=60)
a = json.loads(r.stdout.splitlines()[0])
check("not started says why", r.returncode == 1 and not a["success"] and "LLM profile" in a["error"], a)
check("...and leaves nothing", len(list(jobs.workspace_base().iterdir())) == n)

print("rd_jobs:", "ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
