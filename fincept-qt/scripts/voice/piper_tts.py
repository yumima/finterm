#!/usr/bin/env python3
"""
piper_tts.py — text → WAV via Piper (local TTS).

One-shot CLI: reads the input text from stdin, writes a WAV file to
the path supplied via --output, prints either
    {"status": "ok", "path": "..."}
or
    {"fatal": "..."}
on stdout when done.  Designed for QProcess invocation from
TtsService.

Why Piper: ONNX-shaped, ~30 MB model, runs at >real-time on CPU,
clean Apache-2.0 + MIT licence, native CLI.  No GPU required.

Environment variables (injected by TtsService):
    FINCEPT_TTS_MODEL    Piper model path (.onnx) — required.
                          See https://github.com/rhasspy/piper#voices
                          for the catalogue.  Example:
                          ~/.local/share/finterm/piper/en_US-amy-medium.onnx
    FINCEPT_TTS_LENGTH   Speech length scale (default 1.0; <1 = faster).
    FINCEPT_TTS_NOISE    Noise scale (default 0.667; controls expressiveness).

First-run: user must download a Piper voice model and point
FINCEPT_TTS_MODEL at it.  We don't bundle voices because the smallest
en-US voice alone is ~30 MB and finterm would balloon if we shipped
every locale.

Usage from TtsService (C++ side):
    QProcess piper;
    piper.setProgram("python3");
    piper.setArguments({piper_tts_py, "--output", wav_path});
    piper.setProcessEnvironment(env_with_FINCEPT_TTS_MODEL);
    piper.start();
    piper.write(text.toUtf8());
    piper.closeWriteChannel();
    piper.waitForFinished(30000);
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def write_status(payload: dict) -> None:
    """Emit one JSON-lines status object to stdout and flush."""
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


def _model_sample_rate(model: str) -> int:
    """Piper voice output rate from the model's sidecar .json (default 22050)."""
    try:
        with open(model + ".json", "r", encoding="utf-8") as f:
            return int(json.load(f).get("audio", {}).get("sample_rate", 22050))
    except Exception:
        return 22050


def stream_play(model: str, text: str, piper_bin: str) -> int:
    """Stream synthesis straight to the speakers: `piper --output_raw | <player>`.

    Audio starts within ~1-2s instead of after the whole clip is synthesised
    (~20s for a long article on CPU). TtsService runs this python process as a
    process-group leader and tears the whole pipeline (this python + bash +
    piper + player) down with one group signal on stop(), so no signal plumbing
    is needed here — and there's nothing to orphan even on SIGKILL.
    """
    rate = _model_sample_rate(model)
    if shutil.which("paplay"):
        player = f"paplay --raw --rate={rate} --format=s16le --channels=1"
    elif shutil.which("pw-play"):
        player = f"pw-play --rate={rate} --format=s16 --channels=1 --raw -"
    elif shutil.which("aplay"):
        player = f"aplay -q -r {rate} -f S16_LE -c 1 -t raw -"
    else:
        write_status({"fatal": "no raw audio player (paplay/pw-play/aplay) for streaming"})
        return 2

    length = os.environ.get("FINCEPT_TTS_LENGTH", "1.0")
    noise = os.environ.get("FINCEPT_TTS_NOISE", "0.667")
    pipeline = (f"{shlex.quote(piper_bin)} --model {shlex.quote(model)} --output_raw "
                f"--length_scale {shlex.quote(length)} --noise_scale {shlex.quote(noise)} | {player}")

    # No start_new_session: the pipeline stays in this python's process group so
    # TtsService's group-kill reaches it.
    proc = subprocess.Popen(["bash", "-c", pipeline], stdin=subprocess.PIPE)
    try:
        proc.stdin.write(text.encode("utf-8"))
        proc.stdin.close()
        # Tell TtsService the pipeline launched → it flips the button to STOP. (Audio
        # becomes audible a beat later, once piper loads the model and emits the first
        # chunk — there's no cleaner "first sample played" signal from the player.)
        write_status({"status": "streaming"})
        rc = proc.wait()
    except BrokenPipeError:
        proc.terminate()
        proc.wait()  # reap so we don't leave a zombie behind on early failure
        write_status({"fatal": "piper pipeline closed early"})
        return 2
    if rc != 0:
        write_status({"fatal": f"stream pipeline exited {rc}"})
        return rc
    write_status({"status": "ok"})
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",
                        help="Path to write the synthesised WAV to (file mode).")
    parser.add_argument("--stream", action="store_true",
                        help="Stream audio to the speakers as it synthesises (low latency).")
    args = parser.parse_args()

    text = sys.stdin.read().strip()
    if not text:
        write_status({"fatal": "no text on stdin"})
        return 2

    model = os.environ.get("FINCEPT_TTS_MODEL", "").strip()
    if not model:
        write_status({"fatal": "FINCEPT_TTS_MODEL not set — point at a Piper .onnx voice"})
        return 2
    if not Path(model).exists():
        write_status({"fatal": f"model not found: {model}"})
        return 2

    # Piper CLI: `piper --model <path> --output_file <path>`, reads
    # text from stdin.  Two install shapes work the same way:
    #   - `pip install piper-tts` → `piper` on PATH
    #   - native binary download → user puts it on PATH
    piper_bin = shutil.which("piper") or shutil.which("piper-tts")
    if not piper_bin:
        write_status({
            "fatal": "piper not on PATH — install with `pip install piper-tts` "
                     "or download from https://github.com/rhasspy/piper/releases"
        })
        return 2

    if args.stream:
        return stream_play(model, text, piper_bin)

    if not args.output:
        write_status({"fatal": "--output is required in file mode"})
        return 2

    cmd = [
        piper_bin,
        "--model", model,
        "--output_file", args.output,
        "--length_scale", os.environ.get("FINCEPT_TTS_LENGTH", "1.0"),
        "--noise_scale", os.environ.get("FINCEPT_TTS_NOISE", "0.667"),
    ]

    try:
        # Piper reads text from stdin.  30s ceiling on the subprocess
        # so a misconfigured model can't hang the chat surface forever.
        proc = subprocess.run(cmd, input=text, text=True,
                              capture_output=True, timeout=30)
    except subprocess.TimeoutExpired:
        write_status({"fatal": "piper subprocess timed out after 30s"})
        return 2
    except OSError as exc:
        write_status({"fatal": f"failed to run piper: {exc}"})
        return 2

    if proc.returncode != 0:
        write_status({"fatal": f"piper exited {proc.returncode}: "
                               f"{proc.stderr.strip()[:300]}"})
        return proc.returncode

    if not Path(args.output).exists():
        write_status({"fatal": "piper succeeded but no WAV file written"})
        return 2

    write_status({"status": "ok", "path": args.output})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
