#include "screens/settings/SchedulerSection.h"

#include "services/agents/AgentScheduler.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {
constexpr int kColId = 0;
constexpr int kColAgent = 1;
constexpr int kColSkill = 2;
constexpr int kColCron = 3;
constexpr int kColEnabled = 4;
constexpr int kColLastRun = 5;
constexpr int kColLastStatus = 6;
constexpr int kColCount = 7;
} // namespace

SchedulerSection::SchedulerSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    reload();
}

void SchedulerSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* hdr = new QLabel(QStringLiteral(
        "<b>Scheduler</b> — agent runs that fire on a cron schedule.  "
        "Grammar: <code>@daily HH:MM</code> or <code>@every Nm</code> / "
        "<code>@every Nh</code>."));
    hdr->setWordWrap(true);
    root->addWidget(hdr);

    auto* btn_row = new QHBoxLayout;
    add_btn_ = new QPushButton(QStringLiteral("Add…"));
    remove_btn_ = new QPushButton(QStringLiteral("Remove"));
    toggle_btn_ = new QPushButton(QStringLiteral("Disable"));
    run_now_btn_ = new QPushButton(QStringLiteral("Run now"));
    refresh_btn_ = new QPushButton(QStringLiteral("Refresh"));
    btn_row->addWidget(add_btn_);
    btn_row->addWidget(remove_btn_);
    btn_row->addWidget(toggle_btn_);
    btn_row->addWidget(run_now_btn_);
    btn_row->addStretch();
    btn_row->addWidget(refresh_btn_);
    root->addLayout(btn_row);

    connect(add_btn_, &QPushButton::clicked, this, &SchedulerSection::on_add);
    connect(remove_btn_, &QPushButton::clicked, this, &SchedulerSection::on_remove);
    connect(toggle_btn_, &QPushButton::clicked, this, &SchedulerSection::on_toggle_enabled);
    connect(run_now_btn_, &QPushButton::clicked, this, &SchedulerSection::on_run_now);
    connect(refresh_btn_, &QPushButton::clicked, this, &SchedulerSection::on_refresh);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kColCount);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("Agent"), QStringLiteral("Skill"),
         QStringLiteral("Cron"), QStringLiteral("Enabled"),
         QStringLiteral("Last run"), QStringLiteral("Last status")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColId, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &SchedulerSection::on_selection_changed);
    root->addWidget(table_, 1);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    root->addWidget(status_lbl_);

    on_selection_changed(); // disable per-row buttons until a row is picked
}

QString SchedulerSection::selected_id() const {
    const auto sel = table_->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return {};
    const auto* item = table_->item(sel.first().row(), kColId);
    return item ? item->text() : QString{};
}

bool SchedulerSection::selected_enabled() const {
    const auto sel = table_->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return false;
    const auto* item = table_->item(sel.first().row(), kColEnabled);
    return item && item->text() == QStringLiteral("yes");
}

void SchedulerSection::on_selection_changed() {
    const bool has = !selected_id().isEmpty();
    remove_btn_->setEnabled(has);
    toggle_btn_->setEnabled(has);
    run_now_btn_->setEnabled(has);
    toggle_btn_->setText(has && selected_enabled()
                             ? QStringLiteral("Disable")
                             : QStringLiteral("Enable"));
}

void SchedulerSection::reload() {
    auto r = services::AgentScheduler::instance().list_entries();
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to load: %1")
                        .arg(QString::fromStdString(r.error())), true);
        table_->setRowCount(0);
        return;
    }
    const auto& rows = r.value();
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& e = rows[i];
        table_->setItem(i, kColId, new QTableWidgetItem(e.id));
        table_->setItem(i, kColAgent, new QTableWidgetItem(e.agent_id));
        table_->setItem(i, kColSkill, new QTableWidgetItem(e.skill));
        table_->setItem(i, kColCron, new QTableWidgetItem(e.cron));
        table_->setItem(i, kColEnabled,
                        new QTableWidgetItem(e.enabled ? QStringLiteral("yes")
                                                        : QStringLiteral("no")));
        table_->setItem(i, kColLastRun, new QTableWidgetItem(e.last_run_at));
        table_->setItem(i, kColLastStatus, new QTableWidgetItem(e.last_status));
    }
    on_selection_changed();
}

void SchedulerSection::on_refresh() { reload(); }

void SchedulerSection::on_add() {
    // Small modal — agent_id, skill, cron, args_json.  Validation
    // happens server-side via save_entry's parse_cron call; we just
    // surface the error in the status label.
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add scheduled run"));
    auto* form = new QFormLayout(&dlg);

    auto* agent_edit = new QLineEdit;
    agent_edit->setPlaceholderText(QStringLiteral("e.g. market_researcher"));
    auto* skill_edit = new QLineEdit;
    skill_edit->setPlaceholderText(QStringLiteral("e.g. morning-note"));
    auto* cron_edit = new QLineEdit;
    cron_edit->setPlaceholderText(QStringLiteral("@daily 06:30  /  @every 30m  /  @every 4h"));
    auto* args_edit = new QPlainTextEdit;
    args_edit->setPlaceholderText(QStringLiteral("{} (JSON object; optional)"));
    args_edit->setMaximumHeight(80);

    form->addRow(QStringLiteral("Agent ID"), agent_edit);
    form->addRow(QStringLiteral("Skill"), skill_edit);
    form->addRow(QStringLiteral("Cron"), cron_edit);
    form->addRow(QStringLiteral("Args (JSON)"), args_edit);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    services::ScheduleEntry entry;
    entry.agent_id = agent_edit->text().trimmed();
    entry.skill = skill_edit->text().trimmed();
    entry.cron = cron_edit->text().trimmed();
    entry.args_json = args_edit->toPlainText().trimmed();
    entry.enabled = true;

    if (entry.agent_id.isEmpty() || entry.skill.isEmpty() || entry.cron.isEmpty()) {
        show_status(QStringLiteral("Agent / skill / cron are required"), true);
        return;
    }

    auto r = services::AgentScheduler::instance().save_entry(entry);
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    show_status(QStringLiteral("Added scheduled run"));
    reload();
}

void SchedulerSection::on_remove() {
    const QString id = selected_id();
    if (id.isEmpty())
        return;
    auto r = services::AgentScheduler::instance().remove_entry(id);
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    show_status(QStringLiteral("Removed %1").arg(id));
    reload();
}

void SchedulerSection::on_toggle_enabled() {
    const QString id = selected_id();
    if (id.isEmpty())
        return;
    const bool target = !selected_enabled();
    auto r = services::AgentScheduler::instance().set_enabled(id, target);
    if (r.is_err()) {
        show_status(QString::fromStdString(r.error()), true);
        return;
    }
    show_status(QStringLiteral("%1 %2").arg(target ? "Enabled" : "Disabled", id));
    reload();
}

void SchedulerSection::on_run_now() {
    // Forces an immediate tick — fires every due entry, not just the
    // selected one.  The selected entry is the user's mental anchor
    // but the scheduler API ticks the whole set; that's by design
    // (avoid divergence between scheduled and manual paths).
    const int fired = services::AgentScheduler::instance().tick_now();
    show_status(QStringLiteral("Forced tick — %1 entry(ies) fired").arg(fired));
    reload();
}

void SchedulerSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
