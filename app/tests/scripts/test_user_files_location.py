"""What finterm writes on the user's behalf lives with the user's data, never
beside the scripts: an app update replaces the program's folder, and in an
AppImage it is read-only."""
import atexit
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
TMP = Path(tempfile.mkdtemp(prefix="finterm-user-files-"))
atexit.register(shutil.rmtree, TMP, True)
for var, sub in (("FINTERM_DATA_DIR", "root"), ("FINTERM_USER_DIR", "user"), ("FINTERM_USER_DATA_DIR", "user/data"),
                 ("FINTERM_CACHE_DIR", "cache")):
    os.environ[var] = str(TMP / sub)
sys.dont_write_bytecode = True
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("ok   - " if ok else "FAIL - ") + name + (f"  {detail}" if not ok else ""))
    fails += not ok


# RD-Agent's task folders
sys.path.insert(0, str(SCRIPTS / "agents" / "rdagents"))
import task_manager  # noqa: E402
check("rdagents workspaces are with the user's data",
      task_manager.workspace_base() == TMP / "user" / "features" / "rdagents" / "workspaces", task_manager.workspace_base())
tm = task_manager.TaskManager()
tm.create("t1", "factor_mining", {})
check("a task's folder is one of them", tm.get("t1")["workspace"].startswith(str(TMP)), tm.get("t1")["workspace"])

# Exchange market lists (ccxt is not needed for the path)
sys.modules.setdefault("ccxt", types.ModuleType("ccxt"))
sys.path.insert(0, str(SCRIPTS / "exchange"))
import exchange_client  # noqa: E402
check("exchange market lists are in the cache",
      exchange_client.get_markets_cache_path("x").startswith(str(TMP / "cache")))

# Any line of a shipped script that names a file or folder to write beside itself
# (or, as a bare name, in the folder it happens to run in).
pattern = re.compile(r"__file__.*(workspaces|\.cache\b|\.log\b|\.db\b|\.sqlite)"
                     r"|(\bget\([^)]*,|\bor|=) *f?['\"](\./[^'\"]*|\w+)\.(db|sqlite3?)['\"]")
bad = [f"{p.relative_to(SCRIPTS)}:{n}" for p in SCRIPTS.rglob("*.py") if "tests" not in p.relative_to(SCRIPTS).parts
       for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1) if pattern.search(line)]
check("(scripts were looked through)", len(list(SCRIPTS.rglob("*.py"))) > 100)
check("no script keeps a log, cache, database or workspace beside itself", not bad, bad)

# The user's copy of a skill is the one compared
DIFF = SCRIPTS / "agents" / "ci" / "vendor_skills_diff.py"
shipped, upstream, mine = TMP / "shipped", TMP / "upstream", TMP / "mine"
for root, text in ((shipped, "shipped"), (upstream / "skills", "new")):
    d = root / "v" / "comps"
    d.mkdir(parents=True)
    (d / "SKILL.md").write_text(text)


def diff(*args):
    r = subprocess.run([sys.executable, str(DIFF), "--upstream", str(upstream), "--json", *args],
                       capture_output=True, text=True)
    return json.loads(r.stdout)


rep = diff("--local", str(shipped), "--overlay", str(mine))
check("no copy of the user's: the shipped skill is compared",
      [(e["name"], e["rel_path"]) for e in rep["diverged"]] == [("comps", "v/comps/SKILL.md")], rep)
(mine / "v" / "comps").mkdir(parents=True)
(mine / "v" / "comps" / "SKILL.md").write_text("new")
rep = diff("--local", str(shipped), "--overlay", str(mine))
check("the user's copy is compared instead", rep["diverged"] == [] and rep["vendored"] == ["comps"], rep)
rep = diff()
check("by default, the skills beside the script (in an install too)",
      rep["local_root"] == str(SCRIPTS / "agents" / "finagent_core" / "skills") and rep["local_only"], rep["local_root"])

print(f"{fails} failure(s)")
sys.exit(1 if fails else 0)
