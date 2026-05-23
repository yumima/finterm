#include "screens/settings/ArtefactsSection.h"

#include "services/agents/AgentService.h"

#include <QColor>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr int kArtListLimit = 200;

constexpr int kArtColId = 0;
constexpr int kArtColTitle = 1;
constexpr int kArtColKind = 2;
constexpr int kArtColAgent = 3;
constexpr int kArtColSkill = 4;
constexpr int kArtColStatus = 5;
constexpr int kArtColCreated = 6;
constexpr int kArtColCount = 7;

/// Render an artefact's payload into the export string for the
/// caller's file write.  Format follows the kind; falls back to
/// pretty JSON when the payload doesn't match the expected shape.
struct Exported {
    QString text;
    QString suggested_suffix;  // ".csv" / ".md" / ".json"
};

Exported render_for_export(const ChatArtefactRow& a) {
    const QJsonDocument doc = QJsonDocument::fromJson(a.payload_json.toUtf8());
    const QJsonObject p = doc.object();

    if (a.kind == "table") {
        // {columns: [...], rows: [[...]]}.  Emit RFC-4180 CSV; fall
        // back to pretty JSON when the shape isn't right so the
        // user never gets an empty file.
        const QJsonArray cols = p.value("columns").toArray();
        const QJsonArray rows = p.value("rows").toArray();
        if (!cols.isEmpty()) {
            QStringList lines;
            auto escape = [](const QString& v) -> QString {
                QString s = v;
                const bool needs_quote = s.contains(',') || s.contains('"')
                                         || s.contains('\n') || s.contains('\r');
                s.replace("\"", "\"\"");
                return needs_quote ? "\"" + s + "\"" : s;
            };
            QStringList header;
            for (const auto& v : cols)
                header.append(escape(v.toString()));
            lines.append(header.join(","));
            for (const auto& r : rows) {
                QStringList cells;
                for (const auto& v : r.toArray())
                    cells.append(escape(v.toVariant().toString()));
                lines.append(cells.join(","));
            }
            return {lines.join("\r\n") + "\r\n", ".csv"};
        }
    }

    if (a.kind == "memo" || a.kind == "report") {
        // {sections: [{heading, body}]}.  Emit markdown.
        const QJsonArray sections = p.value("sections").toArray();
        if (!sections.isEmpty()) {
            QStringList parts;
            parts.append("# " + a.title);
            parts.append("");
            for (const auto& s : sections) {
                const QJsonObject obj = s.toObject();
                const QString heading = obj.value("heading").toString();
                const QString body = obj.value("body").toString();
                if (!heading.isEmpty())
                    parts.append("## " + heading);
                if (!body.isEmpty())
                    parts.append(body);
                parts.append("");
            }
            return {parts.join("\n"), ".md"};
        }
    }

    // Default: pretty-print the payload JSON.
    return {QString::fromUtf8(doc.toJson(QJsonDocument::Indented)), ".json"};
}

} // anonymous namespace

ArtefactsSection::ArtefactsSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    reload();
}

void ArtefactsSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* hdr = new QLabel(QStringLiteral(
        "<b>Artefacts</b> — typed slash output (comps tables, models, "
        "memos, reports).  Re-runnable + exportable, never lost in chat scrollback."));
    hdr->setWordWrap(true);
    root->addWidget(hdr);

    auto* btn_row = new QHBoxLayout;
    rerun_btn_ = new QPushButton(QStringLiteral("Re-run"));
    export_btn_ = new QPushButton(QStringLiteral("Export…"));
    supersede_btn_ = new QPushButton(QStringLiteral("Mark superseded"));
    lineage_btn_ = new QPushButton(QStringLiteral("Lineage…"));
    refresh_btn_ = new QPushButton(QStringLiteral("Refresh"));
    btn_row->addWidget(rerun_btn_);
    btn_row->addWidget(export_btn_);
    btn_row->addWidget(supersede_btn_);
    btn_row->addWidget(lineage_btn_);
    btn_row->addStretch();
    btn_row->addWidget(refresh_btn_);
    root->addLayout(btn_row);

    connect(rerun_btn_, &QPushButton::clicked, this, &ArtefactsSection::on_rerun);
    connect(export_btn_, &QPushButton::clicked, this, &ArtefactsSection::on_export);
    connect(supersede_btn_, &QPushButton::clicked, this, &ArtefactsSection::on_supersede);
    connect(lineage_btn_, &QPushButton::clicked, this, &ArtefactsSection::on_lineage);
    connect(refresh_btn_, &QPushButton::clicked, this, &ArtefactsSection::on_refresh);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kArtColCount);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("Title"), QStringLiteral("Kind"),
         QStringLiteral("Agent"), QStringLiteral("Skill"),
         QStringLiteral("Status"), QStringLiteral("Created")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kArtColId, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->setToolTip(QStringLiteral("Double-click to view the full payload."));
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &ArtefactsSection::on_double_clicked);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &ArtefactsSection::on_selection_changed);
    root->addWidget(table_, 1);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    root->addWidget(status_lbl_);

    on_selection_changed();
}

void ArtefactsSection::reload() {
    auto r = ChatArtefactRepository::instance().list_recent(kArtListLimit);
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to load artefacts: %1")
                        .arg(QString::fromStdString(r.error())), true);
        table_->setRowCount(0);
        return;
    }
    populate(r.value());
}

void ArtefactsSection::on_refresh() { reload(); }

void ArtefactsSection::populate(const QVector<ChatArtefactRow>& rows) {
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& a = rows[i];
        // Stash the full id (table cell shows truncated for readability).
        auto* id_item = new QTableWidgetItem(a.id.left(8));
        id_item->setData(Qt::UserRole, a.id);
        id_item->setToolTip(a.id);
        table_->setItem(i, kArtColId, id_item);
        table_->setItem(i, kArtColTitle, new QTableWidgetItem(a.title));
        table_->setItem(i, kArtColKind, new QTableWidgetItem(a.kind));
        table_->setItem(i, kArtColAgent, new QTableWidgetItem(a.source_agent_id));
        table_->setItem(i, kArtColSkill, new QTableWidgetItem(a.source_skill));
        table_->setItem(i, kArtColStatus, new QTableWidgetItem(a.status));
        table_->setItem(i, kArtColCreated, new QTableWidgetItem(a.created_at));
    }
    on_selection_changed();
}

void ArtefactsSection::on_selection_changed() {
    const bool has = !selected_id().isEmpty();
    rerun_btn_->setEnabled(has);
    export_btn_->setEnabled(has);
    supersede_btn_->setEnabled(has);
    lineage_btn_->setEnabled(has);
}

QString ArtefactsSection::selected_id() const {
    const auto sel = table_->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return {};
    const auto* item = table_->item(sel.first().row(), kArtColId);
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

Result<ChatArtefactRow> ArtefactsSection::selected_artefact() const {
    const QString id = selected_id();
    if (id.isEmpty())
        return Result<ChatArtefactRow>::err("nothing selected");
    auto r = ChatArtefactRepository::instance().get(id);
    if (r.is_err())
        return Result<ChatArtefactRow>::err(r.error());
    if (!r.value().has_value())
        return Result<ChatArtefactRow>::err("artefact not found");
    return Result<ChatArtefactRow>::ok(*r.value());
}

void ArtefactsSection::on_double_clicked(int row, int /*column*/) {
    auto* item = table_->item(row, kArtColId);
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    auto r = ChatArtefactRepository::instance().get(id);
    if (r.is_err() || !r.value().has_value())
        return;
    const auto a = *r.value();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Artefact ") + a.title);
    dlg.resize(800, 600);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* meta = new QLabel(QStringLiteral(
        "<b>%1</b> · kind: %2 · agent: %3 · skill: %4 · status: %5 · created: %6")
                                .arg(a.title, a.kind, a.source_agent_id,
                                     a.source_skill, a.status, a.created_at));
    meta->setWordWrap(true);
    layout->addWidget(meta);

    auto* payload_view = new QPlainTextEdit;
    payload_view->setReadOnly(true);
    const QJsonDocument doc = QJsonDocument::fromJson(a.payload_json.toUtf8());
    payload_view->setPlainText(
        doc.isObject() || doc.isArray()
            ? QString::fromUtf8(doc.toJson(QJsonDocument::Indented))
            : a.payload_json);
    layout->addWidget(payload_view, 1);

    auto* close_btn = new QPushButton(QStringLiteral("Close"));
    connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout;
    br->addStretch();
    br->addWidget(close_btn);
    layout->addLayout(br);
    dlg.exec();
}

void ArtefactsSection::on_rerun() {
    auto r = selected_artefact();
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    const ChatArtefactRow a = r.value();
    if (a.source_agent_id.isEmpty() || a.source_skill.isEmpty()) {
        show_status(QStringLiteral(
            "Cannot re-run — no source agent / skill recorded.  "
            "Artefacts created outside slash dispatch don't carry "
            "the re-run handle."), true);
        return;
    }

    // Build dispatch payload first.  We mark the predecessor
    // superseded ONLY after run_agent succeeds — otherwise a dispatch
    // failure (or a model that fails to call emit_artefact later)
    // leaves the user with a superseded predecessor and no
    // replacement.  Better to over-conserve here: failed re-run keeps
    // the original visible, user retries.
    QJsonObject config;
    config[QStringLiteral("agent_id")] = a.source_agent_id;
    config[QStringLiteral("skill")] = a.source_skill;
    config[QStringLiteral("source")] = QStringLiteral("artefact_rerun");
    config[QStringLiteral("artefact_predecessor_id")] = a.id;
    QJsonDocument args_doc = QJsonDocument::fromJson(a.source_args_json.toUtf8());
    if (args_doc.isObject())
        config[QStringLiteral("args")] = args_doc.object();

    QString query = QStringLiteral("Run skill `%1`").arg(a.source_skill);
    if (args_doc.isObject() && !args_doc.object().isEmpty()) {
        query += QStringLiteral(" with args ") +
                 QString::fromUtf8(QJsonDocument(args_doc.object()).toJson(QJsonDocument::Compact));
    }

    QString req_id;
    try {
        req_id = services::AgentService::instance().run_agent(query, config);
    } catch (const std::exception& ex) {
        show_status(QStringLiteral("Re-run dispatch failed: %1 — predecessor "
                                   "left active for retry")
                        .arg(QString::fromUtf8(ex.what())), true);
        return;
    }

    // Dispatch accepted (request_id assigned).  Now safe to mark
    // predecessor superseded; the new emit_artefact (when the agent
    // runs it) will land as the replacement.  Note: if the agent
    // doesn't call emit_artefact at all, the user is left with a
    // superseded predecessor + no new artefact.  Documented behaviour;
    // user can still see the predecessor in the table and manually
    // un-supersede via the row's "status" cell once we wire editing
    // there.
    if (auto sup = ChatArtefactRepository::instance().mark_superseded(a.id);
        sup.is_err()) {
        show_status(QStringLiteral("Re-running %1 (request %2) — note: "
                                   "failed to mark predecessor superseded: %3")
                        .arg(a.title, req_id.left(8),
                             QString::fromStdString(sup.error())), false);
    } else {
        show_status(QStringLiteral("Re-running %1 (request %2)")
                        .arg(a.title, req_id.left(8)));
    }
    reload();
}

void ArtefactsSection::on_export() {
    auto r = selected_artefact();
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    const ChatArtefactRow a = r.value();
    const Exported out = render_for_export(a);

    QString safe_title = a.title;
    safe_title.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), "_");
    const QString suggested = safe_title + out.suggested_suffix;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export artefact"), suggested,
        QStringLiteral("All files (*.*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        show_status(QStringLiteral("Failed to open %1: %2").arg(path, f.errorString()),
                    true);
        return;
    }
    f.write(out.text.toUtf8());
    f.close();
    show_status(QStringLiteral("Exported to %1").arg(path));
}

void ArtefactsSection::on_supersede() {
    const QString id = selected_id();
    if (id.isEmpty())
        return;
    const auto choice = QMessageBox::question(
        this, QStringLiteral("Mark superseded"),
        QStringLiteral("Mark this artefact as superseded?  It stays in the "
                       "table for audit but drops off the active filter."),
        QMessageBox::Yes | QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;
    auto r = ChatArtefactRepository::instance().mark_superseded(id);
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    show_status(QStringLiteral("Superseded %1").arg(id.left(8)));
    reload();
}

void ArtefactsSection::on_lineage() {
    const QString id = selected_id();
    if (id.isEmpty())
        return;
    auto r = ChatArtefactRepository::instance().list_lineage_for(id);
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    const auto rows = r.value();
    if (rows.isEmpty()) {
        show_status(QStringLiteral("Lineage empty for %1").arg(id.left(8)), true);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Lineage — %1").arg(rows.first().title));
    dlg.resize(720, 420);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* meta = new QLabel(QStringLiteral(
        "Re-run chain for this artefact, newest first.  Resolved by "
        "explicit <code>supersedes_id</code> link when present (v039+), "
        "with a fallback to grouping by dispatch identity "
        "(<b>agent</b> · <b>skill</b> · <b>args</b>) for legacy rows.  "
        "The selected artefact is highlighted; predecessors (typically "
        "<code>superseded</code>) trail underneath."));
    meta->setWordWrap(true);
    meta->setStyleSheet(QStringLiteral("color:#aaa;"));
    layout->addWidget(meta);

    auto* tbl = new QTableWidget;
    tbl->setColumnCount(4);
    tbl->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("Status"),
         QStringLiteral("Created"), QStringLiteral("Title")});
    tbl->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tbl->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->verticalHeader()->setVisible(false);
    tbl->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& a = rows[i];
        auto* id_item = new QTableWidgetItem(a.id.left(8));
        id_item->setToolTip(a.id);
        tbl->setItem(i, 0, id_item);
        auto* status_item = new QTableWidgetItem(a.status);
        if (a.status == QStringLiteral("superseded"))
            status_item->setForeground(QColor("#888"));
        tbl->setItem(i, 1, status_item);
        tbl->setItem(i, 2, new QTableWidgetItem(a.created_at));
        tbl->setItem(i, 3, new QTableWidgetItem(a.title));
        if (a.id == id) {
            for (int c = 0; c < 4; ++c)
                tbl->item(i, c)->setBackground(QColor(60, 90, 130));
        }
    }
    layout->addWidget(tbl, 1);

    auto* close_btn = new QPushButton(QStringLiteral("Close"));
    connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout;
    br->addStretch();
    br->addWidget(close_btn);
    layout->addLayout(br);
    dlg.exec();
}

void ArtefactsSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
