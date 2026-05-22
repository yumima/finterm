#pragma once
// SkillProposalService.h — generate a SKILL.md edit proposal from a
// wrong-marked trace (Track 7C).
//
// The flow: user reviews a trace they think went wrong, marks it
// `verdict='wrong'` (Track 7B), and asks for a proposed SKILL.md
// patch.  This service:
//
//   1. Resolves the skill name from the trace's config_json to the
//      on-disk SKILL.md path.
//   2. Reads the current SKILL.md body.
//   3. Crafts a prompt: "here's the current skill spec, here's a
//      turn it produced that the user marked wrong + their note.
//      Propose a minimal replacement SKILL.md that would prevent
//      this next time."
//   4. Calls LlmService::chat synchronously (caller wraps in
//      QtConcurrent to avoid blocking the UI thread).
//
// Apply is deliberately NOT in scope here.  The proposal is text;
// the user copies / pastes (or saves manually) so accidental writes
// to a checked-in skill file can't happen from a button-click.

#include "core/result/Result.h"
#include "storage/repositories/AgentTraceRepository.h"

#include <QString>

namespace fincept::services {

struct SkillProposal {
    QString skill_name;        ///< extracted from trace config_json
    QString skill_path;        ///< absolute path to the SKILL.md
    QString original_content;  ///< the SKILL.md as it stands today
    QString proposed_content;  ///< model's suggested replacement
    QString rationale;         ///< model's one-paragraph reasoning
};

class SkillProposalService {
  public:
    static SkillProposalService& instance();

    /// Resolve a skill name to its on-disk SKILL.md path.  Searches
    /// `<app>/scripts/agents/finagent_core/skills/**/SKILL.md` for a
    /// front-matter `name: <skill_name>` match, falling back to a
    /// directory-name match for skills whose front-matter is missing.
    /// Returns empty string when nothing matches.
    QString resolve_skill_path(const QString& skill_name) const;

    /// Generate a proposal.  Blocking — call from a background
    /// thread via QtConcurrent::run.  Returns Err on:
    ///   - missing skill name in config_json
    ///   - SKILL.md path resolution failure
    ///   - file read failure
    ///   - LlmService not configured / API error
    Result<SkillProposal> propose(const AgentTraceRow& trace,
                                  const QString& user_note) const;

    SkillProposalService(const SkillProposalService&) = delete;
    SkillProposalService& operator=(const SkillProposalService&) = delete;

  private:
    SkillProposalService() = default;
};

} // namespace fincept::services
