#include "screens/settings/ActivityPanelSection.h"

#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr int kPollIntervalMs = 3000;
constexpr int kListLimit = 50;
constexpr int kRecentRows = 15;

// Column layout matches the AI System section's trace table so the
// user's mental model carries over.
constexpr int kActColStarted = 0;
constexpr int kActColAgent = 1;
constexpr int kActColStatus = 2;
constexpr int kActColLatency = 3;
constexpr int kActColCost = 4;
constexpr int kActColSource = 5;
constexpr int kActColCount = 6;

QString act_format_usd(double v) { return QStringLiteral("$%1").arg(QString::number(v, 'f', 2)); }
QString act_format_ms(int v)     { return QStringLiteral("%1 ms").arg(v); }

} // anonymous namespace

ActivityPanelSection::ActivityPanelSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kPollIntervalMs);
    connect(poll_timer_, &QTimer::timeout, this, &ActivityPanelSection::on_refresh);
}

void ActivityPanelSection::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    on_refresh();
    poll_timer_->start();
}

void ActivityPanelSection::hideEvent(QHideEvent* e) {
    poll_timer_->stop();
    QWidget::hideEvent(e);
}

QString ActivityPanelSection::elapsed(const QString& started_at_iso) {
    // SQLite datetime('now') gives "YYYY-MM-DD HH:MM:SS" in UTC.
    // QDateTime needs a TimeSpec hint to parse correctly.
    QDateTime started = QDateTime::fromString(started_at_iso, "yyyy-MM-dd HH:mm:ss");
    started.setTimeSpec(Qt::UTC);
    if (!started.isValid())
        return QStringLiteral("?");
    const qint64 secs = started.secsTo(QDateTime::currentDateTimeUtc());
    if (secs < 0)
        return QStringLiteral("0 s");
    if (secs < 60)
        return QString::number(secs) + " s";
    if (secs < 3600)
        return QString::number(secs / 60) + " m " + QString::number(secs % 60) + " s";
    return QString::number(secs / 3600) + " h " + QString::number((secs % 3600) / 60) + " m";
}

void ActivityPanelSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* hdr = new QLabel(QStringLiteral(
        "<b>Activity</b> — what the agent is doing right now and what it just finished.  "
        "Auto-refreshes every 3 s while this panel is open."));
    hdr->setWordWrap(true);
    root->addWidget(hdr);

    auto* hdr_row = new QHBoxLayout;
    summary_lbl_ = new QLabel(QStringLiteral("—"));
    summary_lbl_->setWordWrap(true);
    refresh_btn_ = new QPushButton(QStringLiteral("Refresh"));
    connect(refresh_btn_, &QPushButton::clicked, this, &ActivityPanelSection::on_refresh);
    hdr_row->addWidget(summary_lbl_, 1);
    hdr_row->addWidget(refresh_btn_);
    root->addLayout(hdr_row);

    auto* in_flight_lbl = new QLabel(QStringLiteral("<b>In flight</b>"));
    root->addWidget(in_flight_lbl);
    in_flight_table_ = new QTableWidget(this);
    in_flight_table_->setColumnCount(kActColCount);
    in_flight_table_->setHorizontalHeaderLabels(
        {QStringLiteral("Started"), QStringLiteral("Agent"),
         QStringLiteral("Elapsed"), QStringLiteral("Latency"),
         QStringLiteral("Cost"), QStringLiteral("Source")});
    in_flight_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    in_flight_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    in_flight_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    in_flight_table_->verticalHeader()->setVisible(false);
    in_flight_table_->setToolTip(QStringLiteral("Double-click a row to view the trace."));
    connect(in_flight_table_, &QTableWidget::cellDoubleClicked,
            this, &ActivityPanelSection::on_view_trace);
    root->addWidget(in_flight_table_, 1);

    auto* recent_lbl = new QLabel(QStringLiteral("<b>Recently finished</b>"));
    root->addWidget(recent_lbl);
    recent_table_ = new QTableWidget(this);
    recent_table_->setColumnCount(kActColCount);
    recent_table_->setHorizontalHeaderLabels(
        {QStringLiteral("Started"), QStringLiteral("Agent"),
         QStringLiteral("Status"), QStringLiteral("Latency"),
         QStringLiteral("Cost"), QStringLiteral("Source")});
    recent_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    recent_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recent_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recent_table_->verticalHeader()->setVisible(false);
    connect(recent_table_, &QTableWidget::cellDoubleClicked,
            this, &ActivityPanelSection::on_view_trace);
    root->addWidget(recent_table_, 1);
}

void ActivityPanelSection::on_refresh() {
    auto r = AgentTraceRepository::instance().list_recent(kListLimit);
    if (r.is_err()) {
        summary_lbl_->setText(QStringLiteral("(failed to load)"));
        return;
    }
    populate(r.value());
}

void ActivityPanelSection::populate(const QVector<AgentTraceRow>& rows) {
    QVector<AgentTraceRow> in_flight;
    QVector<AgentTraceRow> recent;
    for (const auto& t : rows) {
        if (t.status == QStringLiteral("in_progress"))
            in_flight.append(t);
        else if (recent.size() < kRecentRows)
            recent.append(t);
    }
    summary_lbl_->setText(QStringLiteral(
        "%1 in flight · %2 recent (top %3 of last %4 traces)")
                              .arg(in_flight.size())
                              .arg(recent.size())
                              .arg(kRecentRows)
                              .arg(kListLimit));

    in_flight_table_->setRowCount(in_flight.size());
    for (int i = 0; i < in_flight.size(); ++i) {
        const auto& t = in_flight[i];
        auto* started = new QTableWidgetItem(t.started_at);
        started->setData(Qt::UserRole, t.request_id);
        in_flight_table_->setItem(i, kActColStarted, started);
        in_flight_table_->setItem(i, kActColAgent, new QTableWidgetItem(t.agent_id));
        in_flight_table_->setItem(i, kActColStatus, new QTableWidgetItem(elapsed(t.started_at)));
        in_flight_table_->setItem(i, kActColLatency, new QTableWidgetItem(
            t.latency_ms ? act_format_ms(*t.latency_ms) : QString()));
        in_flight_table_->setItem(i, kActColCost, new QTableWidgetItem(
            t.cost_usd ? act_format_usd(*t.cost_usd) : QString()));
        in_flight_table_->setItem(i, kActColSource, new QTableWidgetItem(t.source));
    }

    recent_table_->setRowCount(recent.size());
    for (int i = 0; i < recent.size(); ++i) {
        const auto& t = recent[i];
        auto* started = new QTableWidgetItem(t.started_at);
        started->setData(Qt::UserRole, t.request_id);
        recent_table_->setItem(i, kActColStarted, started);
        recent_table_->setItem(i, kActColAgent, new QTableWidgetItem(t.agent_id));
        recent_table_->setItem(i, kActColStatus, new QTableWidgetItem(t.status));
        recent_table_->setItem(i, kActColLatency, new QTableWidgetItem(
            t.latency_ms ? act_format_ms(*t.latency_ms) : QString()));
        recent_table_->setItem(i, kActColCost, new QTableWidgetItem(
            t.cost_usd ? act_format_usd(*t.cost_usd) : QString()));
        recent_table_->setItem(i, kActColSource, new QTableWidgetItem(t.source));
    }
}

void ActivityPanelSection::on_view_trace(int row, int /*column*/) {
    // Resolve which table fired the signal and pull the request_id
    // off its UserRole.  Then show the same detail dialog the AI
    // System section uses — but cheap enough to inline here rather
    // than couple the two sections.
    auto* sender_table = qobject_cast<QTableWidget*>(sender());
    if (!sender_table)
        return;
    auto* item = sender_table->item(row, kActColStarted);
    if (!item)
        return;
    const QString request_id = item->data(Qt::UserRole).toString();
    if (request_id.isEmpty())
        return;
    auto r = AgentTraceRepository::instance().get(request_id);
    if (r.is_err() || !r.value().has_value())
        return;
    show_trace_dialog(*r.value());
}

void ActivityPanelSection::show_trace_dialog(const AgentTraceRow& t) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Trace ") + t.request_id.left(8));
    dlg.resize(720, 580);
    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 16, 16, 16);

    auto* meta = new QLabel(QStringLiteral(
        "<b>%1</b> · agent: %2 · status: %3 · runtime: %4 · source: %5 · started: %6")
                                .arg(t.request_id.left(12), t.agent_id, t.status,
                                     t.runtime, t.source, t.started_at));
    meta->setWordWrap(true);
    root->addWidget(meta);

    if (!t.query.isEmpty()) {
        root->addWidget(new QLabel(QStringLiteral("<b>Query</b>")));
        auto* q = new QPlainTextEdit;
        q->setReadOnly(true);
        q->setPlainText(t.query);
        root->addWidget(q, 1);
    }
    if (!t.response.isEmpty()) {
        root->addWidget(new QLabel(QStringLiteral("<b>Response</b>")));
        auto* r = new QPlainTextEdit;
        r->setReadOnly(true);
        r->setPlainText(t.response);
        root->addWidget(r, 2);
    }
    if (!t.error.isEmpty()) {
        root->addWidget(new QLabel(QStringLiteral("<b>Error</b>")));
        auto* e = new QPlainTextEdit;
        e->setReadOnly(true);
        e->setPlainText(t.error);
        e->setStyleSheet(QStringLiteral("color: #c33;"));
        root->addWidget(e, 1);
    }

    auto* close_btn = new QPushButton(QStringLiteral("Close"));
    connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout;
    br->addStretch();
    br->addWidget(close_btn);
    root->addLayout(br);
    dlg.exec();
}

} // namespace fincept::screens
