#include "services/agents/SkillProposalService.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "services/agents/SkillStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace finterm::services {

namespace {

constexpr const char* kSkillTag = "SkillProposalService";

/// Extract `name: <value>` from a YAML front-matter block.  Returns
/// empty when the file has no front-matter or no name key.
QString read_frontmatter_name(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(QStringLiteral("\\A---\\s*\\n(.*?)\\n---\\s*\\n"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(content);
    if (!m.hasMatch())
        return {};
    const QString fm = m.captured(1);
    static const QRegularExpression name_re(QStringLiteral("(?m)^name:\\s*(\\S+)"));
    const auto nm = name_re.match(fm);
    return nm.hasMatch() ? nm.captured(1) : QString();
}

QString build_prompt(const QString& skill_md, const QString& query, const QString& response, const QString& user_note) {
    // We instruct the model to return JSON with two top-level keys
    // ("rationale" + "skill_md") so the caller can split rationale
    // (shown above the textbox) from the new SKILL.md (shown in
    // the textbox).  Plain markdown would conflate the two and
    // require regex-splitting on a fence boundary.
    return QStringLiteral("You are revising a finterm agent skill spec based on user feedback.\n\n"
                          "Current SKILL.md:\n```markdown\n%1\n```\n\n"
                          "A user dispatched this skill with the query:\n%2\n\n"
                          "The agent's response was:\n%3\n\n"
                          "The user marked this turn WRONG with note:\n%4\n\n"
                          "Propose a minimal revision to SKILL.md that would prevent this "
                          "failure mode next time.  Keep the YAML front-matter intact.  "
                          "Make the SMALLEST change needed — usually a clarifying sentence "
                          "or an explicit don't-do-X bullet.  Preserve existing workflow "
                          "steps unless they directly caused the failure.\n\n"
                          "Return a JSON object with two keys:\n"
                          "  \"rationale\": one short paragraph (≤80 words) explaining what changed and why.\n"
                          "  \"skill_md\": the complete revised SKILL.md content as a single string.\n"
                          "Return only the JSON object, no surrounding prose.")
        .arg(skill_md, query, response, user_note);
}

} // anonymous namespace

SkillProposalService& SkillProposalService::instance() {
    static SkillProposalService s;
    return s;
}

QString SkillProposalService::resolve_skill(const QString& skill_name) const {
    if (skill_name.isEmpty())
        return {};
    const QString root = skill_store::shipped_root();
    if (root.isEmpty())
        return {};

    QDirIterator it(root, {QStringLiteral("SKILL.md")}, QDir::Files, QDirIterator::Subdirectories);
    QString fallback;
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = skill_store::relative_to(root, path);
        if (rel.isEmpty())
            continue;
        // (The shipped file names the skill; the user's copy may have lost it.)
        if (read_frontmatter_name(path) == skill_name)
            return rel;
        // Directory-name fallback for skills whose front-matter is
        // missing or malformed.  E.g. .../skills/foo/bar/SKILL.md
        // matches skill name "bar".
        const QString dir_name = QFileInfo(path).absoluteDir().dirName();
        if (fallback.isEmpty() && dir_name == skill_name)
            fallback = rel;
    }
    return fallback;
}

Result<SkillProposal> SkillProposalService::propose(const AgentTraceRow& trace, const QString& user_note) const {
    // Pull the skill name out of config_json — written by the slash
    // dispatcher as config["skill"].
    const QJsonDocument doc = QJsonDocument::fromJson(trace.config_json.toUtf8());
    if (!doc.isObject())
        return Result<SkillProposal>::err("trace.config_json is not an object — no skill name to resolve");
    const QString skill_name = doc.object().value(QStringLiteral("skill")).toString();
    if (skill_name.isEmpty())
        return Result<SkillProposal>::err(
            "trace.config_json has no \"skill\" key — propose-fix needs a slash-dispatched turn");

    const QString rel = resolve_skill(skill_name);
    if (rel.isEmpty())
        return Result<SkillProposal>::err(std::string("no SKILL.md found for skill: ") + skill_name.toStdString());

    QFile f(skill_store::in_use(rel));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return Result<SkillProposal>::err(std::string("failed to read SKILL.md: ") + f.errorString().toStdString());
    const QString skill_md = QString::fromUtf8(f.readAll());
    f.close();

    if (!ai_chat::LlmService::instance().is_configured())
        return Result<SkillProposal>::err("LLM service not configured — Settings → AI Config");

    const QString prompt = build_prompt(skill_md, trace.query, trace.response, user_note);

    LOG_INFO(kSkillTag, QString("proposing fix for skill %1 (trace %2)").arg(skill_name, trace.request_id.left(8)));

    // The output is a SKILL.md patch (instructions for an agent), not a
    // statement about markets: number grounding would mangle its examples.
    ai_chat::PersonaScope scope;
    scope.ground_numbers = false;
    auto resp = ai_chat::LlmService::instance().chat(prompt, std::vector<ai_chat::ConversationMessage>{},
                                                     /*use_tools=*/false, scope);
    if (!resp.success)
        return Result<SkillProposal>::err(std::string("LLM error: ") + resp.error.toStdString());

    // Parse the JSON envelope.  Models sometimes wrap their output
    // in a ```json fence — strip that if present before parsing.
    QString raw = resp.content.trimmed();
    if (raw.startsWith(QStringLiteral("```"))) {
        // Drop the opening fence (optionally "```json") and the
        // trailing fence.
        int nl = raw.indexOf('\n');
        if (nl > 0)
            raw = raw.mid(nl + 1);
        if (raw.endsWith(QStringLiteral("```")))
            raw.chop(3);
        raw = raw.trimmed();
    }

    const QJsonDocument env = QJsonDocument::fromJson(raw.toUtf8());
    if (!env.isObject())
        return Result<SkillProposal>::err(std::string("LLM returned non-JSON: ") + raw.left(200).toStdString());

    const QJsonObject obj = env.object();
    SkillProposal out;
    out.skill_name = skill_name;
    out.skill_rel = rel;
    out.original_content = skill_md;
    out.proposed_content = obj.value(QStringLiteral("skill_md")).toString();
    out.rationale = obj.value(QStringLiteral("rationale")).toString();
    if (out.proposed_content.isEmpty())
        return Result<SkillProposal>::err("LLM response missing \"skill_md\" key");
    return Result<SkillProposal>::ok(out);
}

} // namespace finterm::services
