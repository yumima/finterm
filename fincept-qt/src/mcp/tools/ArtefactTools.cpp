#include "mcp/tools/ArtefactTools.h"

#include "storage/repositories/ChatArtefactRepository.h"

#include <QJsonArray>
#include <QStringList>

namespace fincept::mcp::tools {

namespace {

const QStringList kKnownKinds = {"table", "model", "memo", "report"};

QString req_string(const QJsonObject& args, const char* key) {
    return args.value(QString::fromUtf8(key)).toString().trimmed();
}

ToolResult tool_emit_artefact(const QJsonObject& args) {
    const QString kind = req_string(args, "kind");
    const QString title = req_string(args, "title");
    if (kind.isEmpty() || title.isEmpty())
        return ToolResult::fail("emit_artefact requires `kind` and `title`");
    if (!kKnownKinds.contains(kind))
        return ToolResult::fail(
            QStringLiteral("emit_artefact: unknown kind '%1'.  Known: %2")
                .arg(kind, kKnownKinds.join(", ")));

    // Payload is the only required structured field — schema depends
    // on kind, but every kind has *some* payload.  Reject empty so a
    // mis-shaped tool call doesn't write a useless row.
    const QJsonObject payload = args.value("payload").toObject();
    if (payload.isEmpty())
        return ToolResult::fail("emit_artefact: `payload` object is required");

    // Optional provenance.  These usually flow from the slash
    // dispatch's config payload — the chat surface stuffs the
    // request_id / agent_id / skill / args into the agent's
    // ToolContext, which echoes them back into emit_artefact's args.
    // Missing fields are fine; the artefact still saves and renders.
    ChatArtefactCreate c;
    c.kind = kind;
    c.title = title;
    c.payload = payload;
    c.source_request_id = req_string(args, "source_request_id");
    c.source_agent_id = req_string(args, "source_agent_id");
    c.source_skill = req_string(args, "source_skill");
    c.source_args = args.value("source_args").toObject();

    auto r = ChatArtefactRepository::instance().create(c);
    if (r.is_err())
        return ToolResult::fail(QString::fromStdString(r.error()));

    QJsonObject data;
    data["artefact_id"] = r.value();
    data["kind"] = kind;
    data["title"] = title;
    return ToolResult::ok(
        QStringLiteral("Saved %1 artefact '%2'").arg(kind, title), data);
}

} // anonymous namespace

std::vector<ToolDef> get_artefact_tools() {
    std::vector<ToolDef> tools;
    tools.reserve(1);

    ToolDef t;
    t.name = "emit_artefact";
    t.description =
        "Publish a typed structured output (comps table, DCF model, "
        "IC memo, report) to the artefacts store.  Use this instead of "
        "dumping long structured content into chat.  The artefact "
        "surfaces in the Workbench Artefacts panel where the user can "
        "review, export, or re-run it.\n\n"
        "Kinds: table (markdown / CSV comparable-company-style), "
        "model (3-statement / DCF / LBO snapshot), memo (IC / "
        "initiating-coverage / morning-note narrative), report "
        "(multi-section research write-up).";
    t.category = "artefact";
    t.input_schema.properties = QJsonObject{
        {"kind", QJsonObject{
                     {"type", "string"},
                     {"enum", QJsonArray{"table", "model", "memo", "report"}},
                     {"description", "Artefact kind"}}},
        {"title", QJsonObject{
                      {"type", "string"},
                      {"description", "Short user-facing title (e.g. 'AAPL comps 2026Q1')"}}},
        {"payload", QJsonObject{
                        {"type", "object"},
                        {"description",
                         "Structured content. For `table`: {columns: [...], rows: [[...]]}. "
                         "For `model`: {assumptions: {...}, projections: {...}}. "
                         "For `memo`/`report`: {sections: [{heading, body}]}."}}},
        {"source_request_id", QJsonObject{
                                  {"type", "string"},
                                  {"description", "Originating agent_traces.request_id (optional)"}}},
        {"source_agent_id", QJsonObject{
                                {"type", "string"},
                                {"description", "Originating agent id (optional)"}}},
        {"source_skill", QJsonObject{
                             {"type", "string"},
                             {"description", "Originating SKILL.md name (optional)"}}},
        {"source_args", QJsonObject{
                            {"type", "object"},
                            {"description", "Slash args used to produce this artefact — feeds the re-run button"}}},
    };
    t.input_schema.required = {"kind", "title", "payload"};
    t.handler = tool_emit_artefact;
    tools.push_back(std::move(t));

    return tools;
}

} // namespace fincept::mcp::tools
