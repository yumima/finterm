"""A script in a sub-folder is started as a module (`python -m a.b.c`), so its
own folder is not on sys.path: one that imports a sibling by its bare name must
put it there first, and its package's __init__, which runs before, must not
import one at all."""
import ast
import re
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
SCRIPTS = APP / "scripts"
named = set()
for f in (APP / "src").rglob("*.cpp"):
    named.update(re.findall(r'"([A-Za-z_0-9]+(?:/[A-Za-z_0-9]+)+\.py)"', f.read_text(encoding="utf-8", errors="replace")))
named = sorted(n for n in named if (SCRIPTS / n).is_file())
fails = 0


def first_bare_sibling(path):
    """(line, name) of the first top-level import of a file beside `path`, or None."""
    for node in ast.parse(path.read_text(encoding="utf-8")).body:
        names = []
        if isinstance(node, ast.Import):
            names = [a.name for a in node.names]
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            names = [node.module]
        for n in names:
            top = n.split(".")[0]
            if (path.parent / f"{top}.py").is_file() and not (SCRIPTS / f"{top}.py").is_file():
                return node.lineno, top
    return None


for name in named:
    path = SCRIPTS / name
    hit = first_bare_sibling(path)
    if hit and "sys.path." not in "\n".join(path.read_text(encoding="utf-8").splitlines()[: hit[0] - 1]):
        print(f"FAIL - {name}:{hit[0]} imports its sibling {hit[1]} before its folder is on sys.path")
        fails += 1
    folder = path.parent
    while folder != SCRIPTS:
        init = folder / "__init__.py"
        hit = first_bare_sibling(init) if init.is_file() else None
        if hit and "sys.path." not in "\n".join(init.read_text(encoding="utf-8").splitlines()[: hit[0] - 1]):
            print(f"FAIL - {init.relative_to(SCRIPTS)}:{hit[0]} imports its sibling {hit[1]} (runs before {name})")
            fails += 1
        folder = folder.parent
# scripts/ is first on the path: a name here that is a library's hides it
# (scripts/mcp hid the MCP library from every agent).
REQ = [APP / "resources" / f"requirements-numpy{v}.txt" for v in (1, 2)]
libs = {re.split(r"[^A-Za-z0-9_.-]", l.strip())[0].lower().replace("-", "_")
        for r in REQ for l in r.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith(("#", "-"))}
libs |= {"mcp", "edgar", "agno", "rdagent", "qlib", "deepagents"}
for p in SCRIPTS.iterdir():
    if (p.is_dir() and any(p.rglob("*.py")) or p.suffix == ".py") and p.stem.lower() in libs:
        print(f"FAIL - scripts/{p.name} hides the library of that name")
        fails += 1
if len(named) < 30:
    print(f"FAIL - only {len(named)} scripts found in the C++ sources")
    fails += 1
print(f"{len(named)} scripts, {fails} failure(s)")
sys.exit(1 if fails else 0)
