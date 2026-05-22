# CI scripts for the AI stack

Standalone scripts that CI invokes.  No CI provider chosen yet —
each script exits with a sensible code so it slots into GitHub
Actions, Drone, GitLab, or a cron job equally well.

## `nightly_sdk_eval.sh`

Bumps `claude-agent-sdk` to the latest release, runs the
eval-harness tests, exits 0 / 1 / 2.

Wire it as a nightly cron in GitHub Actions:

```yaml
name: nightly-sdk-eval
on:
  schedule:
    - cron: "0 7 * * *"  # daily 07:00 UTC
  workflow_dispatch:

jobs:
  eval:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - run: ./fincept-qt/scripts/agents/ci/nightly_sdk_eval.sh
      - if: always()
        uses: actions/upload-artifact@v4
        with:
          name: eval-runs
          path: ~/.fincept/eval_runs.jsonl
```

A failed nightly = open issue, investigate the regression.  Pass
rate trend lives in the uploaded `eval_runs.jsonl`.

## `vendor_skills_diff.py`

Compares finterm's vendored SKILL.md files (under
`scripts/agents/finagent_core/skills/`) against a local checkout
of `anthropics/financial-services`.  Surfaces:

- `vendored & unchanged`  — happy state
- `vendored & diverged`   — finterm has local edits OR upstream
                            moved; review and decide
- `new upstream`          — content we could vendor
- `local-only`            — finterm-original methodology

Run manually:

```bash
git clone https://github.com/anthropics/financial-services /tmp/fs
./fincept-qt/scripts/agents/ci/vendor_skills_diff.py --upstream /tmp/fs
```

Wire on every PR:

```yaml
name: vendor-skills-diff
on: [pull_request]

jobs:
  skill-diff:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/checkout@v4
        with:
          repository: anthropics/financial-services
          path: /tmp/fs
      - uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - run: |
          ./fincept-qt/scripts/agents/ci/vendor_skills_diff.py \
              --upstream /tmp/fs --json > skill-drift.json
      - if: always()
        uses: actions/upload-artifact@v4
        with:
          name: skill-drift
          path: skill-drift.json
```

Pass `--strict` to make divergence a CI failure (use sparingly —
upstream churn shouldn't block unrelated PRs).
