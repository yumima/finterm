# SKILL.md library

This tree holds the methodology layer for finterm's named agents:
each `<vertical>/<name>/SKILL.md` is a self-contained markdown
prompt the active runtime loads when an agent declares the skill.

Skills are content. Updates are markdown PRs — no Python or C++
changes required.

## Layout

```
skills/
  equity-research/
    morning-note/SKILL.md
    earnings-analysis/SKILL.md
    ...
  investment-banking/
    comps-analysis/SKILL.md
    dcf-model/SKILL.md
    ...
  fund-admin/
  wealth-management/
  ...
```

The directory immediately above `SKILL.md` is the skill's name as
seen by `skills_loader.list_available_skills()` and referenced from
`agent_configs.config_json.skills[]`.

## Vendoring upstream

The eventual plan is to vendor `anthropics/financial-services`
skills under this tree (Apache-2.0). When that lands:

1. Preserve the upstream NOTICE file at `skills/NOTICE` and the
   skill's original frontmatter.
2. Retarget any `data-source-priority` blocks from upstream
   partner MCPs (Daloopa, Morningstar, S&P Kensho, FactSet, etc.)
   to finterm's internal surfaces:
   - `int__fd_*` — financial-datasets REST tools
   - `int__get_news*`, `int__get_filing*`, `int__get_quote*`
   - external `<server_id>__*` tools for marketplace MCPs the user
     has enabled

The three skills already in this tree are finterm-original — not
upstream-vendored — and exist so the named agents in v028 have
something to load while upstream vendoring is sequenced.

## How a skill is loaded

`skills_loader.list_available_skills()` walks this tree looking for
`SKILL.md` files. The Anthropic runtime passes the resulting names
to the Claude Agent SDK via `ClaudeAgentOptions(skills=[...])`, which
resolves the actual file under Claude Code's settings sources.
Materialising finterm's tree under `.claude/skills/<name>/SKILL.md`
is the SDK loader's job at the next iteration; the layout here is
the source of truth.
