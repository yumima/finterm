"""
log_viewer.py — rdagent's own log viewer over the jobs' traces, on this
computer only, for as long as the app keeps the process.

Prints {"success", "url"} once it answers.
"""

from __future__ import annotations

import importlib.util
import json
import os
import socket
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import jobs  # noqa: E402


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _announce(port: int) -> None:
    for _ in range(300):
        with socket.socket() as s:
            s.settimeout(0.5)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                print(json.dumps({"success": True, "url": f"http://127.0.0.1:{port}", "port": port}), flush=True)
                return
        time.sleep(0.2)


def main() -> None:
    spec = importlib.util.find_spec("rdagent")
    try:
        from streamlit.web import cli as streamlit
    except ImportError:
        streamlit = None
    if spec is None or streamlit is None:
        print(json.dumps({"success": False, "error": "rdagent or streamlit is not installed (Settings > Python Env)"}))
        sys.exit(1)
    app = Path(spec.submodule_search_locations[0]) / "log" / "ui" / "app.py"
    jobs.workspace_base().mkdir(parents=True, exist_ok=True)
    os.chdir(jobs.workspace_base())
    port = _free_port()
    threading.Thread(target=_announce, args=(port,), daemon=True).start()
    sys.argv = ["streamlit", "run", str(app), "--server.port", str(port), "--server.address", "127.0.0.1",
                "--server.headless", "true", "--browser.gatherUsageStats", "false", "--",
                f"--log_dir={jobs.workspace_base()}"]
    sys.exit(streamlit.main())


if __name__ == "__main__":
    main()
