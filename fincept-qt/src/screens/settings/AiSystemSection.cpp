#include "screens/settings/AiSystemSection.h"

#include "services/agents/BudgetService.h"
#include "storage/repositories/AgentTraceRepository.h"
#include "storage/repositories/ToolKillswitchRepository.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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
    load_spend();
    load_traces();
    load_killswitch();
}

void AiSystemSection::on_refresh() { reload(); }

void AiSystemSection::load_spend() {
    auto& budget = services::BudgetService::instance();
    auto total = budget.spend_today_total();
    if (total.is_ok()) {
        spend_total_lbl_->setText(
            QStringLiteral("Today's total spend: <b>%1</b>").arg(format_usd(total.value())));
    } else {
        spend_total_lbl_->setText(
            QStringLiteral("Today's total spend: <i>(unavailable)</i>"));
    }

    // Per-agent breakdown — derived from the recent-trace list to
    // avoid a second SQL aggregate.  Approximate for very long
    // histories; over the trace tail window it's exact.
    auto traces = AgentTraceRepository::instance().list_recent(kRecentTracesLimit);
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

void AiSystemSection::load_traces() {
    auto r = AgentTraceRepository::instance().list_recent(kRecentTracesLimit);
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to load traces: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    const auto& rows = r.value();
    traces_table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& t = rows[i];
        traces_table_->setItem(i, 0, new QTableWidgetItem(t.started_at));
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

void AiSystemSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
