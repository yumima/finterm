#!/usr/bin/env bash
# nightly_sdk_eval.sh — bump claude-agent-sdk to head, run the eval
# bench, report pass-rate.  Intended for nightly CI; see README in
# this directory for how to wire into GitHub Actions / Drone / etc.
#
# Exit codes:
#   0  every eval fixture passed (or was skipped because the runtime
#      wasn't ready — the harness treats RuntimeNotReady as skip,
#      not fail)
#   1  one or more fixture assertions failed
#   2  setup error (pip / venv / pytest invocation)
#
# Logs land in $FINCEPT_EVAL_RESULTS or ~/.fincept/eval_runs.jsonl
# via the existing result_store; the CI just exits with the code.

set -euo pipefail

# Repo root resolution — script lives at
# fincept-qt/scripts/agents/ci/, repo root is three levels up.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
AGENTS_DIR="$REPO_ROOT/fincept-qt/scripts/agents"

# Venv lives outside the tree so CI can cache it.  Override via
# FINCEPT_EVAL_VENV.
VENV="${FINCEPT_EVAL_VENV:-/tmp/finterm-eval-venv}"

echo "→ Repo root: $REPO_ROOT"
echo "→ Agents dir: $AGENTS_DIR"
echo "→ Eval venv: $VENV"

if [[ ! -d "$VENV" ]]; then
    echo "→ Creating venv at $VENV"
    python3 -m venv "$VENV"
fi

# Bump claude-agent-sdk to latest; also keep pytest + the eval-harness
# deps current.  Failures here are setup errors (exit 2).
if ! "$VENV/bin/pip" install --quiet --upgrade pip wheel; then
    echo "FATAL: pip self-upgrade failed" >&2
    exit 2
fi
if ! "$VENV/bin/pip" install --quiet --upgrade claude-agent-sdk pytest pyyaml mcp; then
    echo "FATAL: dependency install failed" >&2
    exit 2
fi

INSTALLED_VERSION="$("$VENV/bin/pip" show claude-agent-sdk | awk '/^Version:/{print $2}')"
echo "→ claude-agent-sdk version: $INSTALLED_VERSION"

# Run the eval bench.  The runner's record_run() will append to
# eval_runs.jsonl regardless of how we got there, so a CI artifact
# upload of that file gives a per-fixture trace.
cd "$AGENTS_DIR"
if "$VENV/bin/python" -m pytest evals/tests -q; then
    echo "✓ All eval-harness tests passed (claude-agent-sdk $INSTALLED_VERSION)"
    exit 0
else
    echo "✗ Eval-harness tests failed against claude-agent-sdk $INSTALLED_VERSION" >&2
    exit 1
fi
