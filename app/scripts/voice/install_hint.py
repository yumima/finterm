"""How to add a package voice input needs to the environment it runs in.

finterm's environments are made by uv and may have no pip, and a bare `pip`
is some other Python's: the hint names uv and this interpreter.
"""

import os
import sys


def install_hint(package: str) -> str:
    """The command that installs `package` into the running environment."""
    uv = os.path.join(os.path.dirname(sys.prefix), "uv", "uv.exe" if os.name == "nt" else "uv")
    if os.path.isfile(uv):
        command = f'"{uv}" pip install --python "{sys.executable}" "{package}"'
    else:
        command = f'"{sys.executable}" -m pip install "{package}"'
    if os.name == "nt":
        command = f"& {command}"  # PowerShell reads a leading quoted path as a string
    note = " (in PowerShell)" if os.name == "nt" else ""
    if package == "PyAudio" and os.name != "nt":
        note = " (PortAudio first: apt install portaudio19-dev, or brew install portaudio)"
    return f"run: {command}{note}"
