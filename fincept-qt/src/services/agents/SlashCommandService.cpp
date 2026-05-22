#include "services/agents/SlashCommandService.h"

#include <QJsonArray>

namespace fincept::services {

SlashCommandService& SlashCommandService::instance() {
    static SlashCommandService s;
    return s;
}

namespace {

// In-code registry.  Mirrors Track 7 §7.3 — ~10 representative slash
// commands paired with the named agents v028 seeded.  Add more here
// as skills land; the resolver doesn't need any other changes.
//
// Format: {command, agent_id, skill, positional-arg-keys, help}.
static std::vector<SlashCommandSpec> default_specs() {
    return {
        {QStringLiteral("comps"), QStringLiteral("pitch_agent"),
         QStringLiteral("comps-analysis"),
         {QStringLiteral("ticker")},
         QStringLiteral("Build comps table for a ticker"),
         {QStringLiteral("/dcf {ticker}"), QStringLiteral("/catalysts {ticker}"),
          QStringLiteral("/ic-memo {ticker}")}},

        {QStringLiteral("dcf"), QStringLiteral("model_builder"),
         QStringLiteral("dcf-model"),
         {QStringLiteral("ticker")},
         QStringLiteral("Build / update DCF for a ticker"),
         {QStringLiteral("/comps {ticker}"), QStringLiteral("/initiate {ticker}"),
          QStringLiteral("/thesis {ticker}")}},

        {QStringLiteral("lbo"), QStringLiteral("model_builder"),
         QStringLiteral("lbo-model"),
         {QStringLiteral("ticker")},
         QStringLiteral("Build LBO model for a ticker"),
         {QStringLiteral("/ic-memo {ticker}"), QStringLiteral("/comps {ticker}")}},

        {QStringLiteral("earnings"), QStringLiteral("earnings_reviewer"),
         QStringLiteral("earnings-analysis"),
         {QStringLiteral("ticker"), QStringLiteral("quarter")},
         QStringLiteral("Analyse a printed quarter"),
         {QStringLiteral("/thesis {ticker}"), QStringLiteral("/comps {ticker}"),
          QStringLiteral("/catalysts {ticker}")}},

        {QStringLiteral("earnings-preview"), QStringLiteral("earnings_reviewer"),
         QStringLiteral("earnings-preview"),
         {QStringLiteral("ticker")},
         QStringLiteral("Preview an upcoming earnings print"),
         {QStringLiteral("/catalysts {ticker}"), QStringLiteral("/thesis {ticker}")}},

        {QStringLiteral("initiate"), QStringLiteral("pitch_agent"),
         QStringLiteral("initiating-coverage"),
         {QStringLiteral("ticker")},
         QStringLiteral("Initiating coverage memo"),
         {QStringLiteral("/comps {ticker}"), QStringLiteral("/dcf {ticker}"),
          QStringLiteral("/ic-memo {ticker}")}},

        {QStringLiteral("sector"), QStringLiteral("market_researcher"),
         QStringLiteral("sector-overview"),
         {QStringLiteral("sector")},
         QStringLiteral("Sector overview brief"),
         {QStringLiteral("/morning-note"), QStringLiteral("/rebalance")}},

        {QStringLiteral("ic-memo"), QStringLiteral("pitch_agent"),
         QStringLiteral("ic-memo"),
         {QStringLiteral("ticker")},
         QStringLiteral("Investment-committee memo"),
         {QStringLiteral("/comps {ticker}"), QStringLiteral("/dcf {ticker}"),
          QStringLiteral("/thesis {ticker}")}},

        {QStringLiteral("morning-note"), QStringLiteral("market_researcher"),
         QStringLiteral("morning-note"),
         {},
         QStringLiteral("Today's market open note"),
         {QStringLiteral("/rebalance"), QStringLiteral("/client-review")}},

        {QStringLiteral("catalysts"), QStringLiteral("market_researcher"),
         QStringLiteral("catalyst-calendar"),
         {QStringLiteral("ticker")},
         QStringLiteral("Upcoming catalysts for a ticker"),
         {QStringLiteral("/earnings-preview {ticker}"), QStringLiteral("/thesis {ticker}")}},

        {QStringLiteral("thesis"), QStringLiteral("market_researcher"),
         QStringLiteral("thesis-tracker"),
         {QStringLiteral("ticker")},
         QStringLiteral("Track thesis vs reality"),
         {QStringLiteral("/earnings {ticker} latest"), QStringLiteral("/catalysts {ticker}")}},

        {QStringLiteral("rebalance"), QStringLiteral("month_end_closer"),
         QStringLiteral("portfolio-rebalance"),
         {},
         QStringLiteral("Suggest portfolio rebalance"),
         {QStringLiteral("/morning-note"), QStringLiteral("/client-review")}},

        {QStringLiteral("client-review"), QStringLiteral("meeting_prep"),
         QStringLiteral("client-review"),
         {},
         QStringLiteral("Client review prep pack"),
         {QStringLiteral("/morning-note"), QStringLiteral("/rebalance")}},
    };
}

} // anonymous namespace

SlashCommandService::SlashCommandService() : specs_(default_specs()) {}

bool SlashCommandService::parse_tokens(const QString& input,
                                       QString& out_command,
                                       QStringList& out_positional,
                                       QJsonObject& out_flags) {
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/')))
        return false;

    const QStringList parts = trimmed.split(QChar(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;

    const QString first = parts.first();
    if (first.size() < 2)  // just "/"
        return false;
    out_command = first.mid(1);  // strip leading "/"
    out_positional.clear();
    out_flags = QJsonObject{};

    for (int i = 1; i < parts.size(); ++i) {
        const QString& tok = parts[i];
        if (tok.startsWith(QStringLiteral("--"))) {
            // "--key=value" or "--key" (bool true)
            const int eq = tok.indexOf(QLatin1Char('='));
            if (eq < 0) {
                out_flags[tok.mid(2)] = true;
            } else {
                out_flags[tok.mid(2, eq - 2)] = tok.mid(eq + 1);
            }
        } else {
            out_positional.append(tok);
        }
    }
    return true;
}

std::optional<ResolvedSlash> SlashCommandService::resolve(const QString& input) const {
    QString command;
    QStringList positional;
    QJsonObject flags;
    if (!parse_tokens(input, command, positional, flags))
        return std::nullopt;

    const auto spec = spec_for(command);
    if (!spec)
        return std::nullopt;

    // Map positional tokens into named args using spec.positional.
    // Surplus tokens collect under "_extra"; missing tokens are
    // simply absent — the agent's prompt drives whether they're
    // required.
    QJsonObject args = flags;
    for (int i = 0; i < positional.size(); ++i) {
        if (i < spec->positional.size()) {
            args[spec->positional[i]] = positional[i];
        } else {
            // Surplus — append to "_extra" array.
            QJsonArray extra = args.value(QStringLiteral("_extra")).toArray();
            extra.append(positional[i]);
            args[QStringLiteral("_extra")] = extra;
        }
    }

    ResolvedSlash r;
    r.command = command;
    r.agent_id = spec->agent_id;
    r.skill = spec->skill;
    r.args = args;
    return r;
}

QString SlashCommandService::render_follow_up(const QString& tmpl, const QJsonObject& args) {
    // Substitute every `{key}` placeholder from the args dict.
    // Unknown keys are left as-is so the user sees they need to fill
    // them in.  Replacement is by literal token, not regex
    // back-reference, to keep behaviour predictable when arg values
    // contain `\` or `$`.
    QString out = tmpl;
    static const QRegularExpression re(QStringLiteral("\\{([A-Za-z][A-Za-z0-9_]*)\\}"));
    auto it = re.globalMatch(tmpl);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString token = m.captured(0);
        const QString key = m.captured(1);
        const QString val = args.value(key).toVariant().toString();
        if (!val.isEmpty())
            out.replace(token, val);
    }
    return out;
}

std::optional<SlashCommandSpec> SlashCommandService::spec_for(const QString& command) const {
    for (const auto& s : specs_) {
        if (s.command == command)
            return s;
    }
    return std::nullopt;
}

} // namespace fincept::services
