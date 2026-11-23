#include "ui/navigation/ToolBar.h"

#include "ui/navigation/Perspectives.h"
#include "auth/AuthManager.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QAction>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>

namespace finterm::ui {

static QString menu_ss() {
    return QString("QMenuBar{background:transparent;color:%1;border:none;spacing:0;}"
                   "QMenuBar::item{background:transparent;padding:4px 10px;}"
                   "QMenuBar::item:selected{background:%2;color:%3;}")
        .arg(colors::TEXT_SECONDARY())
        .arg(colors::BG_RAISED())
        .arg(colors::TEXT_PRIMARY());
}

static QString popup_ss() {
    return QString("QMenu{background:%1;color:%2;border:1px solid %3;padding:4px 0;}"
                   "QMenu::item{padding:5px 24px 5px 12px;}"
                   "QMenu::item:selected{background:%4;}"
                   "QMenu::item:disabled{color:%5;}"
                   "QMenu::separator{background:%3;height:1px;margin:4px 8px;}")
        .arg(colors::BG_SURFACE())
        .arg(colors::TEXT_PRIMARY())
        .arg(colors::BORDER_DIM())
        .arg(colors::BG_RAISED())
        .arg(colors::TEXT_SECONDARY());
}

ToolBar::ToolBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(32);
    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(4, 0, 6, 0);
    hl->setSpacing(0);

    // Menus
    menu_bar_ = new QMenuBar(this);
    menu_bar_->setStyleSheet(menu_ss());
    menu_bar_->addMenu(build_file_menu());
    menu_bar_->addMenu(build_navigate_menu());
    menu_bar_->addMenu(build_view_menu());
    menu_bar_->addMenu(build_help_menu());
    hl->addWidget(menu_bar_);

    // Command bar — shrinks gracefully
    command_bar_ = new CommandBar(this);
    command_bar_->setMinimumWidth(80);
    command_bar_->setMaximumWidth(240);
    command_bar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(command_bar_, &CommandBar::navigate_to, this, &ToolBar::navigate_to);
    connect(command_bar_, &CommandBar::dock_command, this, &ToolBar::dock_command);
    hl->addWidget(command_bar_);

    auto sep = [&]() -> QLabel* {
        auto* s = new QLabel("|");
        s->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        hl->addWidget(s);
        separators_.append(s);
        return s;
    };

    auto mk = [](const QString& t) -> QLabel* {
        auto* l = new QLabel(t);
        l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return l;
    };

    sep();
    fin_label_ = mk("FIN");
    hl->addWidget(fin_label_);
    branding_label_ = mk("TERM");
    hl->addWidget(branding_label_);
    subtitle_label_ = mk("  |  PROFESSIONAL RESEARCH DESK");
    hl->addWidget(subtitle_label_);
    hl->addWidget(mk("  "));
    live_dot_ = mk("\xe2\x97\x8f");
    hl->addWidget(live_dot_);
    live_label_ = mk(" LIVE");
    hl->addWidget(live_label_);

    hl->addStretch(1);

    clock_label_ = mk("");
    clock_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    hl->addWidget(clock_label_);

    hl->addStretch(1);

    user_label_ = mk("---");
    user_label_->setMaximumWidth(120);
    hl->addWidget(user_label_);
    sep();

    // Settings sits next to the user name because that is what it now is: one
    // screen for "my account and my setup". It replaces the two separate
    // SETTINGS and PROFILE tabs in the F-key bar. navigate_to() is already
    // handled by MainWindow, so this needs no new plumbing.
    settings_btn_ = new QPushButton("SETTINGS");
    settings_btn_->setFixedHeight(20);
    settings_btn_->setCursor(Qt::PointingHandCursor);
    settings_btn_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(settings_btn_, &QPushButton::clicked, this, [this]() { emit navigate_to(QStringLiteral("settings")); });
    hl->addWidget(settings_btn_);
    sep();

    workbench_btn_ = new QPushButton(QString::fromUtf8("⬡ WORK"));
    workbench_btn_->setFixedHeight(20);
    workbench_btn_->setCursor(Qt::PointingHandCursor);
    workbench_btn_->setToolTip("Open AI Workbench — agents, tools, traces, scheduler");
    workbench_btn_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(workbench_btn_, &QPushButton::clicked, this, &ToolBar::workbench_toggled);
    hl->addWidget(workbench_btn_);
    sep();

    logout_btn_ = new QPushButton("LOGOUT");
    logout_btn_->setFixedHeight(20);
    logout_btn_->setCursor(Qt::PointingHandCursor);
    logout_btn_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(logout_btn_, &QPushButton::clicked, this, &ToolBar::logout_clicked);
    hl->addWidget(logout_btn_);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &ToolBar::update_clock);
    clock_timer_->start();
    update_clock();

    connect(&auth::AuthManager::instance(), &auth::AuthManager::auth_state_changed, this,
            &ToolBar::refresh_user_display);

    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { refresh_theme(); });

    refresh_user_display();
    refresh_theme();
}

void ToolBar::refresh_theme() {
    // Bar background
    setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;").arg(colors::BG_BASE()).arg(colors::BORDER_DIM()));
    // Menu bar + popup menus
    if (menu_bar_) {
        menu_bar_->setStyleSheet(menu_ss());
        for (auto* action : menu_bar_->actions())
            if (action->menu())
                action->menu()->setStyleSheet(popup_ss());
    }
    // Separators
    for (auto* s : separators_)
        s->setStyleSheet(QString("color:%1;background:transparent;padding:0 3px;").arg(colors::TEXT_SECONDARY()));
    // Branding labels
    auto lbl = [](QLabel* l, const QString& c, bool b = false) {
        if (l)
            l->setStyleSheet(QString("color:%1;%2background:transparent;").arg(c, b ? "font-weight:700;" : ""));
    };
    lbl(fin_label_, colors::AMBER(), true);
    lbl(branding_label_, colors::TEXT_PRIMARY(), true);
    lbl(subtitle_label_, colors::TEXT_SECONDARY());
    lbl(live_dot_, colors::POSITIVE());
    lbl(live_label_, colors::POSITIVE(), true);
    lbl(clock_label_, colors::TEXT_PRIMARY());
    lbl(user_label_, colors::AMBER());
    // Buttons
    if (logout_btn_)
        logout_btn_->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:1px solid %1;"
                                           "padding:0 8px;font-weight:700;}"
                                           "QPushButton:hover{background:%1;color:%2;border-color:%1;}")
                                       .arg(colors::NEGATIVE())
                                       .arg(colors::TEXT_PRIMARY()));
}

void ToolBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    apply_responsive_layout(e->size().width());
}

void ToolBar::apply_responsive_layout(int w) {
    // Progressive disclosure: hide less-critical elements as width shrinks
    // >= 1200: everything visible
    // >= 1000: hide subtitle
    //  >= 800: also hide clock, LIVE label
    //  <  800: minimal — menus + branding + user + settings + logout

    bool show_subtitle = (w >= 1200);
    bool show_clock = (w >= 800);
    bool show_live = (w >= 800);

    if (subtitle_label_)
        subtitle_label_->setVisible(show_subtitle);
    if (clock_label_)
        clock_label_->setVisible(show_clock);
    if (live_dot_)
        live_dot_->setVisible(show_live);
    if (live_label_)
        live_label_->setVisible(show_live);
}

void ToolBar::update_clock() {
    auto dt = QDateTime::currentDateTime();
    clock_label_->setText(dt.toString("dd MMM yy").toUpper() + " " + dt.toString("HH:mm:ss"));
}

void ToolBar::refresh_user_display() {
    const auto& s = auth::AuthManager::instance().session();
    if (!s.authenticated) {
        user_label_->setText("---");
        return;
    }

    // Elide username/email to fit within maxWidth
    QString name = s.user_info.username.isEmpty() ? s.user_info.email : s.user_info.username;
    QFontMetrics fm(user_label_->font());
    user_label_->setText(fm.elidedText(name, Qt::ElideRight, user_label_->maximumWidth() - 4));
    user_label_->setToolTip(name);
}

QMenu* ToolBar::build_file_menu() {
    auto* m = new QMenu("File", this);
    m->setStyleSheet(popup_ss());
    m->addAction("New Window", this, [this]() { emit action_triggered("new_window"); });

    // "Move to Monitor" — rebuilt on every popup so plug/unplug events are
    // reflected without restarting the app. Emits "move_to_monitor:<name>"
    // so MainWindow can look the screen up by name (indices are unstable).
    auto* monitors = m->addMenu("Move to Monitor");
    monitors->setStyleSheet(popup_ss());
    connect(monitors, &QMenu::aboutToShow, this, [this, monitors]() {
        monitors->clear();
        const auto screens = QGuiApplication::screens();
        if (screens.size() <= 1) {
            auto* only = monitors->addAction("(single monitor)");
            only->setEnabled(false);
            return;
        }
        int idx = 1;
        for (QScreen* s : screens) {
            const QString name = s->name();
            const QSize size = s->size();
            const QString label = QString("%1. %2  (%3×%4)").arg(idx++).arg(name).arg(size.width()).arg(size.height());
            monitors->addAction(label, this,
                                [this, name]() { emit action_triggered(QString("move_to_monitor:%1").arg(name)); });
        }
    });

    m->addSeparator();
    m->addAction("New Workspace", this, [this]() { emit action_triggered("new_workspace"); });
    m->addAction("Open Workspace", this, [this]() { emit action_triggered("open_workspace"); });
    m->addAction("Save Workspace", this, [this]() { emit action_triggered("save_workspace"); });
    m->addAction("Save Workspace As", this, [this]() { emit action_triggered("save_workspace_as"); });
    m->addSeparator();
    m->addAction("Import Workspace", this, [this]() { emit action_triggered("import_data"); });
    m->addAction("Export Workspace", this, [this]() { emit action_triggered("export_data"); });
    m->addSeparator();
    m->addAction("File Manager", this, [this]() { emit navigate_to("file_manager"); });
    m->addSeparator();
    m->addAction("Refresh All", this, [this]() { emit action_triggered("refresh"); });
    return m;
}

QMenu* ToolBar::build_navigate_menu() {
    auto* m = new QMenu("Navigate", this);
    m->setStyleSheet(QString("QMenu{background:%1;color:%2;border:1px solid %3;padding:2px 0;}"
                             "QMenu::item{padding:3px 20px 3px 10px;}"
                             "QMenu::item:selected{background:%4;}"
                             "QMenu::item:disabled{color:%5;font-weight:700;padding:6px 10px 2px 10px;}"
                             "QMenu::separator{background:%3;height:1px;margin:2px 6px;}"
                             "QMenu::scroller{background:%1;height:14px;}"
                             "QMenu::scroller:hover{background:%4;}")
                         .arg(colors::BG_SURFACE())
                         .arg(colors::TEXT_PRIMARY())
                         .arg(colors::BORDER_DIM())
                         .arg(colors::BG_RAISED())
                         .arg(colors::AMBER()));

    // Use submenus for each group — cleaner and no scroll needed
    auto add_sub = [&](const QString& title) -> QMenu* {
        auto* sub = m->addMenu(title);
        sub->setStyleSheet(m->styleSheet());
        return sub;
    };

    auto nav = [this](QMenu* menu, const QString& label, const QString& id) {
        menu->addAction(label, this, [this, id]() { emit navigate_to(id); });
    };

    // Markets & Data
    auto* mkt = add_sub("Markets & Data");
    nav(mkt, "Futures", "futures");
    nav(mkt, "Economics", "economics");
    nav(mkt, "GOVT Data", "gov_data");
    nav(mkt, "DBnomics", "dbnomics");
    nav(mkt, "AKShare Data", "akshare");
    nav(mkt, "Asia Markets", "asia_markets");
    nav(mkt, "Relationship Map", "relationship_map");

    // Trading & Portfolio
    auto* trd = add_sub("Trading & Portfolio");
    nav(trd, "Equity Trading", "equity_trading");
    nav(trd, "Alpha Arena", "alpha_arena");
    nav(trd, "Prediction Markets", "polymarket");
    nav(trd, "Derivatives", "derivatives");
    nav(trd, "Watchlist", "watchlist");

    // Research & Intelligence
    auto* res = add_sub("Research & Intelligence");
    nav(res, "Equity Research", "equity_research");
    nav(res, "Stock Screener", "screener");
    nav(res, "M&A Analytics", "ma_analytics");
    nav(res, "Alt. Investments", "alt_investments");
    nav(res, "Geopolitics", "geopolitics");
    nav(res, "Maritime", "maritime");
    nav(res, "Surface Analytics", "surface_analytics");

    // Tools
    auto* tools = add_sub("Tools");
    nav(tools, "Agent Config", "agent_config");
    nav(tools, "MCP Servers", "mcp_servers");
    nav(tools, "Data Mapping", "data_mapping");
    nav(tools, "Data Sources", "data_sources");
    nav(tools, "Report Builder", "report_builder");
    nav(tools, "Excel", "excel");
    nav(tools, "Trade Viz", "trade_viz");
    nav(tools, "File Manager", "file_manager");
    nav(tools, "Notes", "notes");

    m->addSeparator();

    // Direct items (no submenu). Social is a top-tab screen too; listed here
    // because no submenu fits publishing.
    nav(m, "Weekly Brief (News)", "social");
    nav(m, "Docs", "docs");
    nav(m, "About", "about");

    return m;
}

QMenu* ToolBar::build_view_menu() {
    auto* m = new QMenu("View", this);
    m->setStyleSheet(popup_ss());
    // Component Browser at the top — Bloomberg's discoverability hook.
    m->addAction("Component Browser\tCtrl+K", this, [this]() { emit action_triggered("browse_components"); });
    m->addSeparator();
    m->addAction("Fullscreen\tF11", this, [this]() { emit action_triggered("fullscreen"); });
    m->addSeparator();
    m->addAction("Focus Mode\tF10", this, [this]() { emit action_triggered("focus_mode"); });
    // Phase 11: the shortcut is Ctrl+Shift+T; we don't mark the QAction as
    // checkable because its state is owned by MainWindow::always_on_top_ —
    // a checkable toolbar action would drift out of sync on window focus
    // changes. If the user cares about the visual, the window's title bar
    // retains the OS-level "always on top" decoration on most platforms.
    m->addAction("Always on Top\tCtrl+Shift+T", this, [this]() { emit action_triggered("always_on_top"); });
    m->addSeparator();

    // Float any screen as a separate window on another monitor
    auto* panels = m->addMenu("Float Panel");
    panels->setStyleSheet(popup_ss());
    panels->addAction("Dashboard", this, [this]() { emit action_triggered("panel_dashboard"); });
    panels->addAction("Watchlist", this, [this]() { emit action_triggered("panel_watchlist"); });
    panels->addAction("News Feed", this, [this]() { emit action_triggered("panel_news"); });
    panels->addAction("Portfolio", this, [this]() { emit action_triggered("panel_portfolio"); });
    panels->addAction("Markets", this, [this]() { emit action_triggered("panel_markets"); });
    panels->addSeparator();
    panels->addAction("Crypto Trading", this, [this]() { emit action_triggered("panel_crypto"); });
    panels->addAction("Equity Trading", this, [this]() { emit action_triggered("panel_equity"); });
    panels->addAction("Algo Trading", this, [this]() { emit action_triggered("panel_algo"); });
    panels->addSeparator();
    panels->addAction("Equity Research", this, [this]() { emit action_triggered("panel_research"); });
    panels->addAction("Economics", this, [this]() { emit action_triggered("panel_economics"); });
    panels->addAction("Geopolitics", this, [this]() { emit action_triggered("panel_geopolitics"); });
    m->addSeparator();

    // Quick Switch — jump to a preset screen layout
    auto* persp = m->addMenu("Quick Switch");
    // Every entry must name a layout in Perspectives.h — the table MainWindow
    // opens from — so a renamed or mistyped action fails loudly here instead
    // of becoming a menu item that does nothing.
    auto add_persp = [this](QMenu* menu, const QString& label, const QString& action) {
        Q_ASSERT_X(perspective_layouts().contains(action), "ToolBar", "Quick Switch action has no layout");
        menu->addAction(label, this, [this, action]() { emit action_triggered(action); });
    };
    persp->setStyleSheet(popup_ss());
    persp->addAction("Save Workspace", this, [this]() { emit action_triggered("perspective_save"); });
    persp->addSeparator();

    // Trading
    auto* qs_trading = persp->addMenu("Trading");
    qs_trading->setStyleSheet(popup_ss());
    add_persp(qs_trading, "Crypto Trading", "perspective_trading");
    add_persp(qs_trading, "Equity Trading", "perspective_equity");
    add_persp(qs_trading, "Algo Trading", "perspective_algo");

    // Research
    auto* qs_research = persp->addMenu("Research");
    qs_research->setStyleSheet(popup_ss());
    add_persp(qs_research, "Equity Research", "perspective_research");
    add_persp(qs_research, "Derivatives", "perspective_derivatives");
    add_persp(qs_research, "M&&A Analytics", "perspective_ma");

    // Portfolio & Markets
    add_persp(persp, "Portfolio View", "perspective_portfolio");
    add_persp(persp, "Markets View", "perspective_markets");
    add_persp(persp, "News View", "perspective_news");

    // Economics & Data
    auto* qs_econ = persp->addMenu("Economics && Data");
    qs_econ->setStyleSheet(popup_ss());
    add_persp(qs_econ, "Economics", "perspective_economics");
    add_persp(qs_econ, "Data Sources", "perspective_data");

    // Geopolitics
    add_persp(persp, "Geopolitics View", "perspective_geopolitics");

    // AI & Quant
    auto* qs_ai = persp->addMenu("AI && Quant");
    qs_ai->setStyleSheet(popup_ss());
    add_persp(qs_ai, "Quant Lab", "perspective_quant");
    add_persp(qs_ai, "Agents + Research", "perspective_ai");

    // Tools
    add_persp(persp, "Tools View", "perspective_tools");
    m->addSeparator();

    m->addAction("Refresh Screen\tF5", this, [this]() { emit action_triggered("refresh"); });
    m->addAction("Take Screenshot\tCtrl+P", this, [this]() { emit action_triggered("screenshot"); });
    return m;
}

QMenu* ToolBar::build_help_menu() {
    auto* m = new QMenu("Help", this);
    m->setStyleSheet(popup_ss());
    m->addAction("About finterm", this, [this]() { emit navigate_to("about"); });
    m->addAction("Help Center", this, [this]() { emit navigate_to("help"); });
    m->addSeparator();
    m->addAction("Check for Updates", this, [this]() { emit action_triggered("check_updates"); });
    m->addSeparator();
    m->addAction("Logout", this, [this]() { emit action_triggered("logout"); });
    return m;
}

} // namespace finterm::ui
