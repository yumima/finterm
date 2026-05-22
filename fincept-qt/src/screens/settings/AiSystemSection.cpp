#include "screens/settings/AiSystemSection.h"

#include "services/agents/BudgetService.h"
#include "storage/repositories/AgentFeedbackRepository.h"
#include "storage/repositories/AgentTraceRepository.h"
#include "storage/repositories/ToolKillswitchRepository.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {
constexpr int kRecentTracesLimit = 50;

QString format_usd(double v) { return QStringLiteral("$%1").arg(QString::number(v, 'f', 2)); }
QString format_ms(int v) { return QStringLiteral("%1 ms").arg(v); }
} // namespace

AiSystemSection::AiSystemSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    reload();
}

void AiSystemSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(16);

    // ── Header ────────────────────────────────────────────────────────
    auto* hdr = new QLabel(QStringLiteral("<b>AI System</b> — agent traces, spend, and tool safety"));
    root->addWidget(hdr);

    refresh_btn_ = new QPushButton(QStringLiteral("Refresh"));
    connect(refresh_btn_, &QPushButton::clicked, this, &AiSystemSection::on_refresh);

    auto* hdr_row = new QHBoxLayout;
    hdr_row->addWidget(refresh_btn_);
    hdr_row->addStretch();
    root->addLayout(hdr_row);

    // ── Spend summary ────────────────────────────────────────────────
    spend_total_lbl_ = new QLabel(QStringLiteral("Today's total: —"));
    spend_by_agent_lbl_ = new QLabel(QStringLiteral("By agent: —"));
    spend_by_agent_lbl_->setWordWrap(true);
    root->addWidget(spend_total_lbl_);
    root->addWidget(spend_by_agent_lbl_);

    // ── Recent agent traces ──────────────────────────────────────────
    auto* traces_lbl = new QLabel(QStringLiteral("Recent agent dispatches:"));
    root->addWidget(traces_lbl);
    traces_table_ = new QTableWidget(this);
    traces_table_->setColumnCount(6);
    traces_table_->setHorizontalHeaderLabels(
        {QStringLiteral("Started"), QStringLiteral("Agent"),
         QStringLiteral("Status"), QStringLiteral("Latency"),
         QStringLiteral("Cost"), QStringLiteral("Source")});
    traces_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    traces_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    traces_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    traces_table_->verticalHeader()->setVisible(false);
    traces_table_->setToolTip(QStringLiteral("Double-click a row to see the full trace."));
    connect(traces_table_, &QTableWidget::cellDoubleClicked,
            this, &AiSystemSection::on_trace_double_clicked);
    root->addWidget(traces_table_, 2);

    // ── Tool kill-switch ─────────────────────────────────────────────
    auto* kill_lbl = new QLabel(QStringLiteral("Disabled tools (kill-switch):"));
    root->addWidget(kill_lbl);
    killswitch_table_ = new QTableWidget(this);
    killswitch_table_->setColumnCount(3);
    killswitch_table_->setHorizontalHeaderLabels(
        {QStringLiteral("Tool"), QStringLiteral("Reason"), QStringLiteral("Disabled at")});
    killswitch_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    killswitch_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    killswitch_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    killswitch_table_->verticalHeader()->setVisible(false);
    root->addWidget(killswitch_table_, 1);

    auto* kill_btns = new QHBoxLayout;
    disable_btn_ = new QPushButton(QStringLiteral("Disable tool…"));
    enable_btn_ = new QPushButton(QStringLiteral("Re-enable selected"));
    connect(disable_btn_, &QPushButton::clicked, this, &AiSystemSection::on_disable_tool);
    connect(enable_btn_, &QPushButton::clicked, this, &AiSystemSection::on_enable_tool);
    kill_btns->addWidget(disable_btn_);
    kill_btns->addWidget(enable_btn_);
    kill_btns->addStretch();
    root->addLayout(kill_btns);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    root->addWidget(status_lbl_);
}

void AiSystemSection::reload() {
    // One trace fetch fuels both the per-agent spend breakdown and
    // the traces table.  Earlier revisions called list_recent twice
    // per reload — wasted SQL on a hot UI path.
    auto traces = AgentTraceRepository::instance().list_recent(kRecentTracesLimit);
    load_spend(traces);
    load_traces(traces);
    load_killswitch();
}

void AiSystemSection::on_refresh() { reload(); }

void AiSystemSection::load_spend(const Result<QVector<AgentTraceRow>>& traces) {
    auto& budget = services::BudgetService::instance();
    auto total = budget.spend_today_total();
    if (total.is_ok()) {
        spend_total_lbl_->setText(
            QStringLiteral("Today's total spend: <b>%1</b>").arg(format_usd(total.value())));
    } else {
        spend_total_lbl_->setText(
            QStringLiteral("Today's total spend: <i>(unavailable)</i>"));
    }

    // Per-agent breakdown — derived from the trace tail (passed in by
    // reload() so we don't query the DB twice).  Approximate for very
    // long histories; over the recent-trace window it's exact.
    if (traces.is_err()) {
        spend_by_agent_lbl_->setText(QStringLiteral("By agent: <i>(unavailable)</i>"));
        return;
    }
    QHash<QString, double> sum;
    for (const auto& r : traces.value()) {
        if (r.cost_usd && !r.agent_id.isEmpty())
            sum[r.agent_id] += *r.cost_usd;
    }
    QStringList parts;
    for (auto it = sum.constBegin(); it != sum.constEnd(); ++it)
        parts.append(QStringLiteral("%1 %2").arg(it.key(), format_usd(it.value())));
    spend_by_agent_lbl_->setText(
        parts.isEmpty()
            ? QStringLiteral("By agent: (no spend yet)")
            : QStringLiteral("By agent (recent): ") + parts.join(QStringLiteral(", ")));
}

void AiSystemSection::load_traces(const Result<QVector<AgentTraceRow>>& r) {
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to load traces: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    const auto& rows = r.value();
    traces_table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& t = rows[i];
        auto* started = new QTableWidgetItem(t.started_at);
        // Stash the request_id on the first cell's UserRole so the
        // double-click handler can fetch the full trace without
        // re-scanning the table.
        started->setData(Qt::UserRole, t.request_id);
        traces_table_->setItem(i, 0, started);
        traces_table_->setItem(i, 1, new QTableWidgetItem(t.agent_id));
        traces_table_->setItem(i, 2, new QTableWidgetItem(t.status));
        traces_table_->setItem(i, 3, new QTableWidgetItem(
                                          t.latency_ms ? format_ms(*t.latency_ms) : QString()));
        traces_table_->setItem(i, 4, new QTableWidgetItem(
                                          t.cost_usd ? format_usd(*t.cost_usd) : QString()));
        traces_table_->setItem(i, 5, new QTableWidgetItem(t.source));
    }
}

void AiSystemSection::load_killswitch() {
    auto r = ToolKillswitchRepository::instance().list_all();
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to load kill-switch: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    const auto& rows = r.value();
    killswitch_table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        killswitch_table_->setItem(i, 0, new QTableWidgetItem(rows[i].tool_name));
        killswitch_table_->setItem(i, 1, new QTableWidgetItem(rows[i].reason));
        killswitch_table_->setItem(i, 2, new QTableWidgetItem(rows[i].disabled_at));
    }
}

void AiSystemSection::on_disable_tool() {
    bool ok = false;
    const QString tool = QInputDialog::getText(
        this, QStringLiteral("Disable tool"),
        QStringLiteral("Wire-form name (e.g. int__get_news, mcp-fetch__fetch):"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || tool.isEmpty())
        return;
    const QString reason = QInputDialog::getText(
        this, QStringLiteral("Reason"),
        QStringLiteral("Reason (shown in refusal message — optional):"),
        QLineEdit::Normal).trimmed();
    auto r = ToolKillswitchRepository::instance().disable(tool, reason);
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to disable: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    show_status(QStringLiteral("Disabled %1").arg(tool));
    load_killswitch();
}

void AiSystemSection::on_enable_tool() {
    const auto sel = killswitch_table_->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        show_status(QStringLiteral("Select a row first"), true);
        return;
    }
    const auto* item = killswitch_table_->item(sel.first().row(), 0);
    if (!item)
        return;
    const QString tool = item->text();
    auto r = ToolKillswitchRepository::instance().enable(tool);
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to enable: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    show_status(QStringLiteral("Re-enabled %1").arg(tool));
    load_killswitch();
}

void AiSystemSection::on_trace_double_clicked(int row, int /*column*/) {
    auto* item = traces_table_->item(row, 0);
    if (!item)
        return;
    const QString request_id = item->data(Qt::UserRole).toString();
    if (request_id.isEmpty())
        return;

    auto r = AgentTraceRepository::instance().get(request_id);
    if (r.is_err() || !r.value().has_value()) {
        show_status(QStringLiteral("Failed to load trace %1: %2")
                        .arg(request_id.left(8),
                             r.is_err() ? QString::fromStdString(r.error())
                                        : QStringLiteral("(not found)")),
                    true);
        return;
    }
    const AgentTraceRow t = *r.value();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Trace ") + t.request_id.left(8));
    dlg.resize(720, 580);
    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    auto add_kv = [&](const QString& label, const QString& value) {
        if (value.isEmpty())
            return;
        auto* row_w = new QWidget;
        auto* hl = new QHBoxLayout(row_w);
        hl->setContentsMargins(0, 0, 0, 0);
        auto* k = new QLabel(QStringLiteral("<b>%1</b>").arg(label));
        k->setFixedWidth(110);
        auto* v = new QLabel(value.toHtmlEscaped());
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        hl->addWidget(k);
        hl->addWidget(v, 1);
        root->addWidget(row_w);
    };

    add_kv(QStringLiteral("Request ID"), t.request_id);
    add_kv(QStringLiteral("Agent"), t.agent_id);
    add_kv(QStringLiteral("Runtime"), t.runtime);
    add_kv(QStringLiteral("Source"), t.source);
    add_kv(QStringLiteral("Status"), t.status);
    add_kv(QStringLiteral("Started"), t.started_at);
    add_kv(QStringLiteral("Finished"), t.finished_at);
    if (t.latency_ms)
        add_kv(QStringLiteral("Latency"), format_ms(*t.latency_ms));
    if (t.tokens_in)
        add_kv(QStringLiteral("Tokens in"), QString::number(*t.tokens_in));
    if (t.tokens_out)
        add_kv(QStringLiteral("Tokens out"), QString::number(*t.tokens_out));
    if (t.cost_usd)
        add_kv(QStringLiteral("Cost"), format_usd(*t.cost_usd));

    auto add_text_block = [&](const QString& label, const QString& text) {
        if (text.isEmpty())
            return;
        root->addSpacing(8);
        root->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(label)));
        auto* edit = new QPlainTextEdit;
        edit->setReadOnly(true);
        edit->setPlainText(text);
        edit->setMinimumHeight(80);
        root->addWidget(edit, 1);
    };
    add_text_block(QStringLiteral("Query"), t.query);
    add_text_block(QStringLiteral("Response"), t.response);
    add_text_block(QStringLiteral("Error"), t.error);
    if (!t.config_json.isEmpty()) {
        // Pretty-print for readability.  Failure (malformed JSON) falls
        // back to the raw string.
        const QJsonDocument doc = QJsonDocument::fromJson(t.config_json.toUtf8());
        const QString pretty = doc.isObject() || doc.isArray()
            ? QString::fromUtf8(doc.toJson(QJsonDocument::Indented))
            : t.config_json;
        add_text_block(QStringLiteral("Config"), pretty);
    }

    // Feedback row — capture user judgement on this turn.  Note is
    // optional; verdict is mandatory.  Track 7B writes to
    // agent_feedback; Track 7C will read the wrong-marked rows to
    // propose SKILL.md edits.  Show any existing feedback first so
    // the user sees what they already said.
    {
        auto prior = AgentFeedbackRepository::instance().list_by_request(t.request_id);
        if (prior.is_ok() && !prior.value().isEmpty()) {
            root->addWidget(new QLabel(QStringLiteral("<b>Prior feedback</b>")));
            QStringList parts;
            for (const auto& f : prior.value()) {
                parts.append(QStringLiteral("%1: %2%3")
                                 .arg(f.created_at, f.verdict,
                                      f.note.isEmpty() ? QString()
                                                       : QStringLiteral(" — ") + f.note));
            }
            auto* p = new QLabel(parts.join("\n"));
            p->setWordWrap(true);
            p->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
            root->addWidget(p);
        }
    }

    auto* mark_wrong = new QPushButton(QStringLiteral("👎 Mark wrong"));
    auto* mark_right = new QPushButton(QStringLiteral("👍 Mark right"));
    auto* close_btn = new QPushButton(QStringLiteral("Close"));
    connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto record_feedback = [this, &dlg, &t](const QString& verdict) {
        bool ok = false;
        const QString note = QInputDialog::getText(
            &dlg, QStringLiteral("Mark ") + verdict,
            QStringLiteral("Optional note (what was wrong / right):"),
            QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        auto r = AgentFeedbackRepository::instance().create(t.request_id, verdict, note);
        if (r.is_err()) {
            show_status(QStringLiteral("Failed to save feedback: %1")
                            .arg(QString::fromStdString(r.error())), true);
            return;
        }
        show_status(QStringLiteral("Recorded %1 for %2").arg(verdict, t.request_id.left(8)));
        dlg.accept();
    };
    connect(mark_wrong, &QPushButton::clicked, this, [record_feedback]() {
        record_feedback(QStringLiteral("wrong"));
    });
    connect(mark_right, &QPushButton::clicked, this, [record_feedback]() {
        record_feedback(QStringLiteral("right"));
    });

    auto* btn_row = new QHBoxLayout;
    btn_row->addWidget(mark_wrong);
    btn_row->addWidget(mark_right);
    btn_row->addStretch();
    btn_row->addWidget(close_btn);
    root->addLayout(btn_row);

    dlg.exec();
}

void AiSystemSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
