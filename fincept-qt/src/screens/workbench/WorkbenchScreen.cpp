#include "screens/workbench/WorkbenchScreen.h"

#include "screens/settings/AiSystemSection.h"
#include "screens/settings/SchedulerSection.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

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
    return build_section_placeholder(
        QStringLiteral("Chat"),
        QStringLiteral("AI Chat with streaming, tool calls, slash commands "
                       "(`/help`, `/comps`, `/dcf`, …), and the active LLM "
                       "profile."),
        QStringLiteral("Today: open the dedicated Chat screen from the "
                       "main toolbar.  Embedding here is the next step."));
}

QWidget* WorkbenchScreen::build_agents_section() {
    return build_section_placeholder(
        QStringLiteral("Agents"),
        QStringLiteral("Named agent identities — Pitch Agent, Meeting Prep, "
                       "Market Researcher, Earnings Reviewer, Model Builder, "
                       "Valuation Reviewer, GL Reconciler, Month-End Closer, "
                       "Statement Auditor, KYC Screener.  Each carries a "
                       "system prompt, skills list, and per-agent tool "
                       "allowlist."),
        QStringLiteral("Today: edit via Settings → Profiles.  Direct edit "
                       "of agent_configs.config_json works in any SQLite browser."));
}

QWidget* WorkbenchScreen::build_teams_section() {
    return build_section_placeholder(
        QStringLiteral("Teams"),
        QStringLiteral("Multi-agent teams — coordinator + member agents "
                       "running shared context and turn-taking strategies."),
        QStringLiteral("Coming soon.  The Anthropic SDK's sub-agent + the "
                       "local runtime's planner give us the primitives; the "
                       "team UI is the wrapper."));
}

QWidget* WorkbenchScreen::build_workflows_section() {
    return build_section_placeholder(
        QStringLiteral("Workflows"),
        QStringLiteral("Node-graph workflows that chain tool calls + LLM "
                       "steps into a repeatable pipeline.  Powers the "
                       "scheduled-run bodies and the `/morning-note` "
                       "skill output."),
        QStringLiteral("Today: open the existing Workflows screen "
                       "from the main toolbar."));
}

QWidget* WorkbenchScreen::build_tools_section() {
    return build_section_placeholder(
        QStringLiteral("Tools"),
        QStringLiteral("Internal MCP tool catalog (32 families: portfolio, "
                       "news, financial-datasets, quant lab, paper trading, "
                       "…) plus external server tools.  Per-agent allowlists "
                       "filter what each named agent can see."),
        QStringLiteral("Today: browse via Settings → MCP Servers → Tools tab."));
}

QWidget* WorkbenchScreen::build_servers_section() {
    return build_section_placeholder(
        QStringLiteral("Servers"),
        QStringLiteral("External MCP servers (stdio + HTTP).  Marketplace "
                       "seed list: mcp-server-fetch, filesystem, sqlite, "
                       "time, sequentialthinking, brave-search, playwright, "
                       "yfinance, financial-datasets/mcp-server."),
        QStringLiteral("Today: open Settings → MCP Servers."));
}

QWidget* WorkbenchScreen::build_profiles_section() {
    return build_section_placeholder(
        QStringLiteral("Profiles"),
        QStringLiteral("LLM profiles (Anthropic / local) — each carries a "
                       "runtime, model, base URL, API-key reference, and "
                       "max-tokens default.  Profile selection drives every "
                       "agent dispatch."),
        QStringLiteral("Today: edit via Settings → LLM Config."));
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
