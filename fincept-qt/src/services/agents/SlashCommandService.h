#pragma once
// SlashCommandService.h — slash command → (agent_id, skill, args) resolver.
//
// Track 7 #21: `/comps AAPL`, `/dcf AAPL --period=quarterly`, etc.
// resolve to a tuple the active runtime can dispatch.  The resolver
// is pure (no I/O, no DB, no QObject):
//
//   const auto r = SlashCommandService::instance().resolve("/comps AAPL");
//   // r = ResolvedSlash{ agent_id="pitch_agent", skill="comps-analysis",
//   //                    args={"ticker":"AAPL"} }
//
// Registry is in-code today.  Future moves to YAML / SQLite when the
// list grows past ~30 commands; the consumer-facing API doesn't
// change.
//
// Argument parsing v1: positional + `--key=value` flags.  Each slash
// command declares its positional arg keys; the parser maps tokens
// in order.  Surplus positionals collect under "_extra"; surplus
// flags pass through verbatim.  Quoting (e.g. `/foo "two words"`) is
// not supported in v1 — single-token args only.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace fincept::services {

/// Resolved slash command — what AgentService::run_agent dispatches.
struct ResolvedSlash {
    QString command;     ///< original command without leading "/"
    QString agent_id;    ///< matches agent_configs.id
    QString skill;       ///< matches a SKILL.md directory name
    QJsonObject args;    ///< positional + flag args
};

/// One row in the registry.
struct SlashCommandSpec {
    QString command;          ///< "comps", "dcf", "earnings" — no leading "/"
    QString agent_id;
    QString skill;
    QStringList positional;   ///< ordered arg keys for positional tokens
    QString help;             ///< one-line user-facing description
    /// Suggested follow-up commands (each one a template the user
    /// can run next).  The chat surface renders these as clickable
    /// chips after a successful dispatch.  Use `{ticker}` etc. as
    /// placeholders — the resolver fills them from the just-run args.
    QStringList follow_ups;
};

class SlashCommandService {
  public:
    static SlashCommandService& instance();

    /// Resolve raw chat input.  Returns nullopt when:
    ///   - input doesn't start with '/'
    ///   - command is unknown
    /// Unknown commands are intentionally silent — the chat layer
    /// will surface "unknown command" itself via list_commands().
    std::optional<ResolvedSlash> resolve(const QString& input) const;

    /// Lower-level: split a "/cmd a b --k=v" string into command + tokens.
    /// Exposed for tests + the chat composer's autocomplete.
    static bool parse_tokens(const QString& input,
                             QString& out_command,
                             QStringList& out_positional,
                             QJsonObject& out_flags);

    /// All registered specs.  Used by /help and the chat composer's
    /// autocomplete pop-up.
    const std::vector<SlashCommandSpec>& list_commands() const { return specs_; }

    /// Look up a single spec by command name (no leading "/").
    std::optional<SlashCommandSpec> spec_for(const QString& command) const;

    /// Render a follow-up template with the resolved args (e.g.
    /// `/catalysts {ticker}` + `{ticker: AAPL}` → `/catalysts AAPL`).
    /// Used by the chat surface after a successful dispatch.
    static QString render_follow_up(const QString& tmpl, const QJsonObject& args);

    SlashCommandService(const SlashCommandService&) = delete;
    SlashCommandService& operator=(const SlashCommandService&) = delete;

  private:
    SlashCommandService();
    std::vector<SlashCommandSpec> specs_;
};

} // namespace fincept::services
