#include "screens/workbench/WorkbenchScreen.h"

#include "mcp/McpService.h"
#include "screens/settings/AiSystemSection.h"
#include "screens/settings/SchedulerSection.h"
#include "storage/repositories/AgentConfigRepository.h"
#include "storage/repositories/ChatRepository.h"
#include "storage/repositories/LlmProfileRepository.h"
#include "storage/repositories/McpServerRepository.h"
#include "storage/repositories/TeamRepository.h"
#include "storage/repositories/ToolKillswitchRepository.h"
#include "storage/repositories/WorkflowRepository.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>

#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <functional>

namespace fincept::screens {

namespace {

struct NavEntry {
    QString label;
    QString description;
    QString cta;
};

// Index order matches WorkbenchScreen::sections_ order.  Add new
// sections at the bottom; the existing indices are stable.
constexpr int kIdxChat = 0;
constexpr int kIdxAgents = 1;
constexpr int kIdxTeams = 2;
constexpr int kIdxWorkflows = 3;
constexpr int kIdxTools = 4;
constexpr int kIdxServers = 5;
constexpr int kIdxProfiles = 6;
constexpr int kIdxSystem = 7;

QString nav_btn_style() {
    return QStringLiteral(
        "QPushButton {"
        "  text-align: left; padding: 8px 12px; border: none;"
        "  background: transparent; color: #ccc;"
        "}"
        "QPushButton:checked {"
        "  background: #2a4a6a; color: white;"
        "}"
        "QPushButton:hover { background: #1f3650; color: white; }"
    );
}

} // anonymous namespace

WorkbenchScreen::WorkbenchScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void WorkbenchScreen::build_ui() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Left nav ──────────────────────────────────────────────────────
    auto* nav = new QWidget;
    nav->setFixedWidth(200);
    nav->setStyleSheet(QStringLiteral("background: #1a1d23;"));
    auto* nav_layout = new QVBoxLayout(nav);
    nav_layout->setContentsMargins(8, 16, 8, 16);
    nav_layout->setSpacing(2);

    auto* title = new QLabel(QStringLiteral("<b>AI Workbench</b>"));
    title->setStyleSheet(QStringLiteral("color: white; font-size: 14px; padding: 0 4px 12px 4px;"));
    nav_layout->addWidget(title);

    sections_ = new QStackedWidget;
    sections_->addWidget(build_chat_section());      // kIdxChat
    sections_->addWidget(build_agents_section());    // kIdxAgents
    sections_->addWidget(build_teams_section());     // kIdxTeams
    sections_->addWidget(build_workflows_section()); // kIdxWorkflows
    sections_->addWidget(build_tools_section());     // kIdxTools
    sections_->addWidget(build_servers_section());   // kIdxServers
    sections_->addWidget(build_profiles_section()); // kIdxProfiles
    sections_->addWidget(build_system_section());    // kIdxSystem

    auto make_btn = [&](const QString& text, int idx) {
        auto* btn = new QPushButton(text);
        btn->setFixedHeight(36);
        btn->setCheckable(true);
        btn->setStyleSheet(nav_btn_style());
        connect(btn, &QPushButton::clicked, this, [this, btn, idx]() {
            for (auto* sibling : nav_btns_)
                sibling->setChecked(false);
            btn->setChecked(true);
            sections_->setCurrentIndex(idx);
        });
        nav_layout->addWidget(btn);
        nav_btns_.push_back(btn);
        return btn;
    };

    auto* first = make_btn(QStringLiteral("Chat"), kIdxChat);
    make_btn(QStringLiteral("Agents"), kIdxAgents);
    make_btn(QStringLiteral("Teams"), kIdxTeams);
    make_btn(QStringLiteral("Workflows"), kIdxWorkflows);
    make_btn(QStringLiteral("Tools"), kIdxTools);
    make_btn(QStringLiteral("Servers"), kIdxServers);
    make_btn(QStringLiteral("Profiles"), kIdxProfiles);
    make_btn(QStringLiteral("System"), kIdxSystem);
    nav_layout->addStretch();

    auto* exit_btn = new QPushButton(QStringLiteral("← Back"));
    exit_btn->setStyleSheet(nav_btn_style());
    connect(exit_btn, &QPushButton::clicked, this, &WorkbenchScreen::exit_requested);
    nav_layout->addWidget(exit_btn);

    first->setChecked(true);
    sections_->setCurrentIndex(kIdxChat);

    root->addWidget(nav);
    root->addWidget(sections_, 1);
}

namespace {

/// Shared layout for read-only catalog tables (Chat, Agents, Tools,
/// Workflows, Servers, Profiles).  Caller provides headers, a
/// stretch-target column index, and a reload functor; this returns a
/// fully-wired QWidget with title / description / Refresh / table.
QWidget* build_table_section(const QString& title_text,
                             const QString& description_html,
                             const QStringList& column_headers,
                             int stretch_col,
                             std::function<void(QTableWidget*)> reload) {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(title_text));
    auto* desc = new QLabel(description_html);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #aaa;"));
    layout->addWidget(title);
    layout->addWidget(desc);

    auto* refresh_btn = new QPushButton(QStringLiteral("Refresh"));
    layout->addWidget(refresh_btn);

    auto* table = new QTableWidget;
    table->setColumnCount(column_headers.size());
    table->setHorizontalHeaderLabels(column_headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (stretch_col >= 0 && stretch_col < column_headers.size())
        table->horizontalHeader()->setSectionResizeMode(stretch_col, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table, 1);

    auto reload_fn = std::move(reload);
    QObject::connect(refresh_btn, &QPushButton::clicked, table,
                     [table, reload_fn]() { reload_fn(table); });
    reload_fn(table);
    return w;
}

} // anonymous namespace

QWidget* WorkbenchScreen::build_section_placeholder(const QString& name,
                                                    const QString& description,
                                                    const QString& cta_text) {
    // Placeholder for sections whose underlying widget hasn't been
    // ported into the Workbench yet.  Tells the user where to find
    // the equivalent today; the eventual port will replace this
    // widget with the real surface.
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(name));
    auto* desc = new QLabel(description);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #aaa;"));
    auto* hint = new QLabel(cta_text);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888; font-style: italic;"));

    layout->addWidget(title);
    layout->addWidget(desc);
    layout->addWidget(hint);
    layout->addStretch();
    return w;
}

QWidget* WorkbenchScreen::build_chat_section() {
    // Recent chat sessions, newest first.  Read-only — opening or
    // resuming a session still goes through the dedicated Chat
    // screen (single owner for chat lifecycle).
    auto reload = [](QTableWidget* table) {
        auto r = ChatRepository::instance().list_sessions();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto sessions = r.value();
        table->setRowCount(sessions.size());
        for (int i = 0; i < sessions.size(); ++i) {
            const auto& s = sessions[i];
            table->setItem(i, 0, new QTableWidgetItem(s.title.isEmpty()
                                                         ? QStringLiteral("(untitled)")
                                                         : s.title));
            table->setItem(i, 1, new QTableWidgetItem(
                                     QStringLiteral("%1 / %2").arg(s.provider, s.model)));
            table->setItem(i, 2, new QTableWidgetItem(QString::number(s.message_count)));
            table->setItem(i, 3, new QTableWidgetItem(s.updated_at));
        }
    };
    return build_table_section(
        QStringLiteral("Chat"),
        QStringLiteral("Recent chat sessions across all profiles.  "
                       "Open or resume from the dedicated Chat screen — "
                       "this view is read-only."),
        {QStringLiteral("Title"), QStringLiteral("Provider / Model"),
         QStringLiteral("Msgs"), QStringLiteral("Updated")},
        /*stretch_col=*/0, std::move(reload));
}

QWidget* WorkbenchScreen::build_agents_section() {
    auto reload = [](QTableWidget* table) {
        auto r = AgentConfigRepository::instance().list_all();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto agents = r.value();
        table->setRowCount(agents.size());
        for (int i = 0; i < agents.size(); ++i) {
            const auto& a = agents[i];
            table->setItem(i, 0, new QTableWidgetItem(a.name));
            table->setItem(i, 1, new QTableWidgetItem(a.category.isEmpty()
                                                         ? QStringLiteral("—")
                                                         : a.category));
            auto* d = new QTableWidgetItem(a.description);
            d->setToolTip(a.description);
            table->setItem(i, 2, d);
            QStringList flags;
            if (a.is_default) flags << QStringLiteral("default");
            if (a.is_active)  flags << QStringLiteral("active");
            table->setItem(i, 3, new QTableWidgetItem(flags.join(QStringLiteral(", "))));
        }
    };
    return build_table_section(
        QStringLiteral("Agents"),
        QStringLiteral("Named agent identities — each carries a system "
                       "prompt, skill list, and per-agent tool allowlist.  "
                       "Edit via <b>Settings → Profiles</b>."),
        {QStringLiteral("Name"), QStringLiteral("Category"),
         QStringLiteral("Description"), QStringLiteral("Flags")},
        /*stretch_col=*/2, std::move(reload));
}

namespace {

/// Modal create/edit dialog for a team.  Caller passes in either an
/// existing TeamRow (edit) or an empty struct (create).  Returns true
/// on save.  Membership is a multi-select list of agent_configs rows;
/// coordinator is a dropdown of the same.
bool show_team_edit_dialog(QWidget* parent, TeamRow& draft, bool is_new) {
    auto agents_res = AgentConfigRepository::instance().list_all();
    QVector<AgentConfig> agents = agents_res.is_ok() ? agents_res.value() : QVector<AgentConfig>{};

    QDialog dlg(parent);
    dlg.setWindowTitle(is_new ? QStringLiteral("New team") : QStringLiteral("Edit team — ") + draft.name);
    dlg.resize(560, 520);
    auto* root = new QVBoxLayout(&dlg);

    auto* form = new QFormLayout;
    auto* id_edit = new QLineEdit(draft.id);
    id_edit->setPlaceholderText(QStringLiteral("kebab-case-id"));
    if (!is_new)
        id_edit->setReadOnly(true);   // primary key — immutable post-create
    auto* name_edit = new QLineEdit(draft.name);
    auto* desc_edit = new QLineEdit(draft.description);
    desc_edit->setPlaceholderText(QStringLiteral("Optional one-line description"));
    auto* coordinator_combo = new QComboBox;
    for (const auto& a : agents) {
        coordinator_combo->addItem(QStringLiteral("%1 — %2").arg(a.id, a.name), a.id);
        if (a.id == draft.coordinator_agent_id)
            coordinator_combo->setCurrentIndex(coordinator_combo->count() - 1);
    }
    auto* strategy_combo = new QComboBox;
    // v037 stores the strategy column but agno's TeamModule.from_config
    // only reads name/description/mode/leader_index/roles — strategy
    // is not yet honored at the runtime layer.  Label honestly so the
    // user doesn't think they're configuring real behavior.  Mapping
    // to agno's `mode` is a follow-up.
    strategy_combo->addItem(QStringLiteral("sequential (persisted, not yet routed)"), "sequential");
    strategy_combo->addItem(QStringLiteral("parallel (persisted, not yet routed)"), "parallel");
    if (draft.strategy == QStringLiteral("parallel"))
        strategy_combo->setCurrentIndex(1);

    form->addRow(QStringLiteral("ID"), id_edit);
    form->addRow(QStringLiteral("Name"), name_edit);
    form->addRow(QStringLiteral("Description"), desc_edit);
    form->addRow(QStringLiteral("Coordinator"), coordinator_combo);
    form->addRow(QStringLiteral("Strategy"), strategy_combo);
    root->addLayout(form);

    auto* members_lbl = new QLabel(QStringLiteral("<b>Members</b> (checked agents weigh in; the coordinator synthesises):"));
    members_lbl->setWordWrap(true);
    root->addWidget(members_lbl);
    auto* members_list = new QListWidget;
    members_list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto& a : agents) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(a.id, a.name), members_list);
        item->setData(Qt::UserRole, a.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(draft.member_agent_ids.contains(a.id) ? Qt::Checked : Qt::Unchecked);
    }
    root->addWidget(members_list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    const QString id = id_edit->text().trimmed();
    const QString name = name_edit->text().trimmed();
    if (id.isEmpty() || name.isEmpty() || coordinator_combo->currentData().toString().isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Save team"),
            QStringLiteral("ID, name, and coordinator are required."));
        return false;
    }
    draft.id = id;
    draft.name = name;
    draft.description = desc_edit->text().trimmed();
    draft.coordinator_agent_id = coordinator_combo->currentData().toString();
    draft.strategy = strategy_combo->currentData().toString();
    draft.member_agent_ids.clear();
    for (int i = 0; i < members_list->count(); ++i) {
        auto* item = members_list->item(i);
        if (item->checkState() == Qt::Checked)
            draft.member_agent_ids.append(item->data(Qt::UserRole).toString());
    }
    return true;
}

} // anonymous namespace

QWidget* WorkbenchScreen::build_teams_section() {
    // Track 98 — real teams panel.  Read-only list + per-row New /
    // Edit / Delete actions.  Dispatch happens via the AI Chat
    // slash command `/team <name> …` (single source of truth — same
    // pattern as `/comps`).
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("<h2>Teams</h2>"));
    auto* desc = new QLabel(QStringLiteral(
        "Multi-agent teams: one <b>coordinator</b> plus N <b>members</b>.  "
        "Members each weigh in (their lens — sector, quant, macro, …); the "
        "coordinator synthesises a unified answer.  Dispatch via "
        "<code>/team &lt;name&gt; &lt;query&gt;</code> in chat."));
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #aaa;"));
    layout->addWidget(title);
    layout->addWidget(desc);

    auto* btn_row = new QHBoxLayout;
    auto* new_btn = new QPushButton(QStringLiteral("New team…"));
    auto* edit_btn = new QPushButton(QStringLiteral("Edit"));
    auto* delete_btn = new QPushButton(QStringLiteral("Delete"));
    auto* refresh_btn = new QPushButton(QStringLiteral("Refresh"));
    btn_row->addWidget(new_btn);
    btn_row->addWidget(edit_btn);
    btn_row->addWidget(delete_btn);
    btn_row->addStretch();
    btn_row->addWidget(refresh_btn);
    layout->addLayout(btn_row);

    auto* table = new QTableWidget;
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("Name"),
         QStringLiteral("Coordinator"), QStringLiteral("Members"),
         QStringLiteral("Strategy")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table, 1);

    auto reload = [table]() {
        auto r = TeamRepository::instance().list_all();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto teams = r.value();
        table->setRowCount(teams.size());
        for (int i = 0; i < teams.size(); ++i) {
            const auto& t = teams[i];
            auto* id_item = new QTableWidgetItem(t.id);
            id_item->setData(Qt::UserRole, t.id);
            table->setItem(i, 0, id_item);
            table->setItem(i, 1, new QTableWidgetItem(t.name));
            table->setItem(i, 2, new QTableWidgetItem(t.coordinator_agent_id));
            table->setItem(i, 3, new QTableWidgetItem(QString::number(t.member_agent_ids.size())));
            table->setItem(i, 4, new QTableWidgetItem(t.strategy));
        }
    };

    auto selected_id = [table]() -> QString {
        const auto sel = table->selectionModel()->selectedRows();
        if (sel.isEmpty())
            return {};
        auto* item = table->item(sel.first().row(), 0);
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    QObject::connect(refresh_btn, &QPushButton::clicked, table, reload);
    QObject::connect(new_btn, &QPushButton::clicked, w, [w, reload]() {
        TeamRow draft;
        if (!show_team_edit_dialog(w, draft, /*is_new=*/true))
            return;
        TeamCreate c;
        c.id = draft.id;
        c.name = draft.name;
        c.coordinator_agent_id = draft.coordinator_agent_id;
        c.strategy = draft.strategy;
        c.description = draft.description;
        c.member_agent_ids = draft.member_agent_ids;
        auto r = TeamRepository::instance().create(c);
        if (r.is_err()) {
            QMessageBox::warning(w, QStringLiteral("Create team"),
                                 QString::fromStdString(r.error()));
            return;
        }
        reload();
    });
    QObject::connect(edit_btn, &QPushButton::clicked, w, [w, selected_id, reload]() {
        const QString id = selected_id();
        if (id.isEmpty())
            return;
        auto r = TeamRepository::instance().get(id);
        if (r.is_err() || !r.value().has_value())
            return;
        TeamRow draft = *r.value();
        if (!show_team_edit_dialog(w, draft, /*is_new=*/false))
            return;
        TeamCreate c;
        c.id = draft.id;
        c.name = draft.name;
        c.coordinator_agent_id = draft.coordinator_agent_id;
        c.strategy = draft.strategy;
        c.description = draft.description;
        c.member_agent_ids = draft.member_agent_ids;
        auto ur = TeamRepository::instance().update(c);
        if (ur.is_err()) {
            QMessageBox::warning(w, QStringLiteral("Update team"),
                                 QString::fromStdString(ur.error()));
            return;
        }
        reload();
    });
    QObject::connect(delete_btn, &QPushButton::clicked, w, [w, selected_id, reload]() {
        const QString id = selected_id();
        if (id.isEmpty())
            return;
        const auto choice = QMessageBox::question(
            w, QStringLiteral("Delete team"),
            QStringLiteral("Delete team <b>%1</b>?\nMembership rows are cascade-deleted; "
                           "agents themselves are untouched.").arg(id),
            QMessageBox::Yes | QMessageBox::No);
        if (choice != QMessageBox::Yes)
            return;
        auto r = TeamRepository::instance().delete_(id);
        if (r.is_err()) {
            QMessageBox::warning(w, QStringLiteral("Delete team"),
                                 QString::fromStdString(r.error()));
            return;
        }
        reload();
    });
    reload();
    return w;
}

QWidget* WorkbenchScreen::build_workflows_section() {
    auto reload = [](QTableWidget* table) {
        auto r = WorkflowRepository::instance().list_all();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto wfs = r.value();
        table->setRowCount(wfs.size());
        for (int i = 0; i < wfs.size(); ++i) {
            const auto& w = wfs[i];
            table->setItem(i, 0, new QTableWidgetItem(w.name));
            table->setItem(i, 1, new QTableWidgetItem(w.status.isEmpty()
                                                         ? QStringLiteral("—")
                                                         : w.status));
            auto* d = new QTableWidgetItem(w.description);
            d->setToolTip(w.description);
            table->setItem(i, 2, d);
            table->setItem(i, 3, new QTableWidgetItem(w.updated_at));
        }
    };
    return build_table_section(
        QStringLiteral("Workflows"),
        QStringLiteral("Node-graph pipelines chaining tool calls + LLM "
                       "steps.  Powers scheduled runs and "
                       "<code>/morning-note</code>.  Edit via the "
                       "dedicated Workflows screen."),
        {QStringLiteral("Name"), QStringLiteral("Status"),
         QStringLiteral("Description"), QStringLiteral("Updated")},
        /*stretch_col=*/2, std::move(reload));
}

QWidget* WorkbenchScreen::build_tools_section() {
    // Real catalog table.  Each row: wire-form name, category,
    // description, kill-switch status.  Refresh re-reads McpService;
    // the table is read-only — disable/enable still lives in the
    // System section's kill-switch UI so the destructive action has
    // one home, not two.
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("<h2>Tools</h2>"));
    auto* desc = new QLabel(QStringLiteral(
        "Every MCP tool the active runtime can call.  Internal tools "
        "(prefixed <code>int__</code>) ship with finterm; external "
        "tools come from MCP servers you've enabled in <b>Settings → "
        "MCP Servers</b>.  Disable / re-enable any tool from "
        "<b>Settings → AI System</b>."));
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #aaa;"));
    layout->addWidget(title);
    layout->addWidget(desc);

    auto* refresh_btn = new QPushButton(QStringLiteral("Refresh"));
    layout->addWidget(refresh_btn);

    auto* table = new QTableWidget;
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Tool"), QStringLiteral("Category"),
         QStringLiteral("Description"), QStringLiteral("Kill-switch")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table, 1);

    auto reload = [table]() {
        const auto tools = mcp::McpService::instance().get_all_tools();
        QSet<QString> disabled;
        auto kr = ToolKillswitchRepository::instance().disabled_names();
        if (kr.is_ok())
            disabled = kr.value();
        table->setRowCount(static_cast<int>(tools.size()));
        for (std::size_t i = 0; i < tools.size(); ++i) {
            const auto& t = tools[i];
            const QString wire = t.server_id + QStringLiteral("__") + t.name;
            const int row = static_cast<int>(i);
            table->setItem(row, 0, new QTableWidgetItem(wire));
            table->setItem(row, 1, new QTableWidgetItem(t.category.isEmpty()
                                                            ? QStringLiteral("—")
                                                            : t.category));
            auto* desc_item = new QTableWidgetItem(t.description);
            desc_item->setToolTip(t.description);
            table->setItem(row, 2, desc_item);
            auto* ks_item = new QTableWidgetItem(
                disabled.contains(wire) ? QStringLiteral("disabled") : QStringLiteral("active"));
            if (disabled.contains(wire))
                ks_item->setForeground(QColor("#c33"));
            table->setItem(row, 3, ks_item);
        }
    };
    QObject::connect(refresh_btn, &QPushButton::clicked, table, reload);
    reload();
    return w;
}

QWidget* WorkbenchScreen::build_servers_section() {
    auto reload = [](QTableWidget* table) {
        auto r = McpServerRepository::instance().list_all();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto servers = r.value();
        table->setRowCount(servers.size());
        for (int i = 0; i < servers.size(); ++i) {
            const auto& s = servers[i];
            table->setItem(i, 0, new QTableWidgetItem(s.name));
            table->setItem(i, 1, new QTableWidgetItem(s.transport_type));
            auto* st = new QTableWidgetItem(s.status.isEmpty()
                                                ? QStringLiteral("stopped")
                                                : s.status);
            if (s.status == QStringLiteral("error"))
                st->setForeground(QColor("#c33"));
            else if (s.status == QStringLiteral("running"))
                st->setForeground(QColor("#3a3"));
            table->setItem(i, 2, st);
            table->setItem(i, 3, new QTableWidgetItem(s.enabled
                                                         ? QStringLiteral("enabled")
                                                         : QStringLiteral("disabled")));
        }
    };
    return build_table_section(
        QStringLiteral("Servers"),
        QStringLiteral("External MCP servers (stdio + HTTP).  Enable / "
                       "configure via <b>Settings → MCP Servers</b>."),
        {QStringLiteral("Name"), QStringLiteral("Transport"),
         QStringLiteral("Status"), QStringLiteral("Enabled")},
        /*stretch_col=*/0, std::move(reload));
}

QWidget* WorkbenchScreen::build_profiles_section() {
    auto reload = [](QTableWidget* table) {
        auto r = LlmProfileRepository::instance().list_profiles();
        if (r.is_err()) {
            table->setRowCount(0);
            return;
        }
        const auto profiles = r.value();
        table->setRowCount(profiles.size());
        for (int i = 0; i < profiles.size(); ++i) {
            const auto& p = profiles[i];
            table->setItem(i, 0, new QTableWidgetItem(p.name));
            table->setItem(i, 1, new QTableWidgetItem(p.provider));
            table->setItem(i, 2, new QTableWidgetItem(p.model_id));
            table->setItem(i, 3, new QTableWidgetItem(p.runtime.isEmpty()
                                                         ? QStringLiteral("—")
                                                         : p.runtime));
            table->setItem(i, 4, new QTableWidgetItem(p.is_default
                                                         ? QStringLiteral("default")
                                                         : QString()));
        }
    };
    return build_table_section(
        QStringLiteral("Profiles"),
        QStringLiteral("LLM profiles (Anthropic / local / external).  "
                       "Profile selection drives every agent dispatch.  "
                       "Edit via <b>Settings → LLM Config</b>."),
        {QStringLiteral("Name"), QStringLiteral("Provider"),
         QStringLiteral("Model"), QStringLiteral("Runtime"),
         QStringLiteral("Default")},
        /*stretch_col=*/0, std::move(reload));
}

QWidget* WorkbenchScreen::build_system_section() {
    // Real content: embed an AiSystemSection (traces / spend /
    // kill-switch) above a SchedulerSection.  Both widgets are pure
    // views over the relevant repositories — instantiating fresh
    // copies here is cheap and avoids re-parenting the instances
    // that live under Settings.
    auto* w = new QWidget;
    auto* scroll = new QScrollArea(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget;
    auto* il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(0);
    il->addWidget(new AiSystemSection);
    il->addWidget(new SchedulerSection);
    scroll->setWidget(inner);

    auto* wl = new QVBoxLayout(w);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->addWidget(scroll);
    return w;
}

} // namespace fincept::screens
