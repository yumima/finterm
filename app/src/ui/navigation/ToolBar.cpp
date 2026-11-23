#include "ui/navigation/ToolBar.h"

#include "services/calendar/MarketClock.h"

#include <QFontMetrics>
#include <algorithm>
#include <cstdlib>

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

namespace {
/// A clock label: it asks for the width of the longest text it can show (so
/// its neighbours don't move as the text changes) and can shrink to nothing
/// (so it never holds the window wider than the room there is).
class ClockLabel : public QLabel {
  public:
    using QLabel::QLabel;
    int widest = 0;
    QSize sizeHint() const override { return {std::max(widest, 1), QLabel::sizeHint().height()}; }
    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }
};
} // namespace

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
    command_bar_->setMinimumWidth(150); // the clocks shrink before it does
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

    // The market's clock (NYSE / Nasdaq: New York time, the session, the
    // countdown to the bell), then the local clock — each labelled.
    market_sep_ = mk("  |  ");
    hl->addWidget(market_sep_);
    market_label_ = new ClockLabel;
    market_label_->setTextFormat(Qt::RichText);
    // Its width is the longest text it can show (size_clocks; ClockLabel),
    // and it can shrink — it is hidden below 1000 px by apply_responsive_layout.
    market_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    market_label_->setToolTip(QStringLiteral("NYSE and Nasdaq regular session, 09:30–16:00 New York time "
                                             "(13:00 on early closes); counts to the close while open, "
                                             "else to the next open. Holidays and early closes per the "
                                             "NYSE calendar."));
    hl->addWidget(market_label_);
    clock_sep_ = mk("  |  ");
    hl->addWidget(clock_sep_);
    clock_label_ = new ClockLabel;
    clock_label_->setTextFormat(Qt::RichText);
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
    lbl(market_label_, colors::TEXT_PRIMARY());
    lbl(market_sep_, colors::BORDER_MED());
    lbl(clock_sep_, colors::BORDER_MED());
    size_clocks();
    update_clock(); // the rich-text colours come from the theme
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
    // >= 2200: everything visible (the subtitle too)
    // >= 1000: no subtitle — the market clock has its room
    //  >= 800: also no market clock
    //  <  800: minimal — menus + branding + user + settings + logout
    //          (no LOCAL clock, no LIVE label)

    bool show_subtitle = (w >= 2200); // the market clock takes its room first
    bool show_market = (w >= 1000);
    bool show_clock = (w >= 800);
    bool show_live = (w >= 800);

    if (subtitle_label_)
        subtitle_label_->setVisible(show_subtitle);
    if (market_label_)
        market_label_->setVisible(show_market);
    if (market_sep_)
        market_sep_->setVisible(show_market);
    if (clock_sep_)
        clock_sep_->setVisible(show_clock);
    if (clock_label_)
        clock_label_->setVisible(show_clock);
    if (live_dot_)
        live_dot_->setVisible(show_live);
    if (live_label_)
        live_label_->setVisible(show_live);
}

void ToolBar::size_clocks() {
    if (!market_label_ || !clock_label_)
        return;
    // The widest each can read (bold state word, a day count, an early close).
    const QFontMetrics fm(market_label_->font());
    QFont bold = market_label_->font();
    bold.setBold(true);
    const QFontMetrics fmb(bold);
    // The forms it takes: an early close is never days away, a weekend wait is.
    const int head = fm.horizontalAdvance(QStringLiteral("NYSE·NASDAQ 88:88:88 ET  "));
    const int market = head + 4 +
                       std::max({fmb.horizontalAdvance(QStringLiteral("OPEN")) +
                                     fm.horizontalAdvance(QStringLiteral(" · EARLY CLOSE IN 88:88:88")),
                                 fmb.horizontalAdvance(QStringLiteral("PRE-OPEN")) +
                                     fm.horizontalAdvance(QStringLiteral(" · OPENS IN 88:88:88")),
                                 fmb.horizontalAdvance(QStringLiteral("CLOSED")) +
                                     fm.horizontalAdvance(QStringLiteral(" · OPENS IN 8d 88:88:88"))});
    const int local = fm.horizontalAdvance(QStringLiteral("LOCAL 88 MMM 88 88:88:88 UTC+88:88")) + 4;
    static_cast<ClockLabel*>(market_label_)->widest = market;
    static_cast<ClockLabel*>(clock_label_)->widest = local;
    market_label_->updateGeometry();
    clock_label_->updateGeometry();
}

void ToolBar::update_clock() {
    if (!clock_label_ || !market_label_)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const auto tag = [](const QString& text) {
        return QStringLiteral("<span style='color:%1;'>%2</span>").arg(colors::TEXT_SECONDARY(), text);
    };
    const auto word = [](const QString& text, const QString& color) {
        return QStringLiteral("<span style='color:%1;font-weight:700;'>%2</span>").arg(color, text);
    };

    // NYSE · NASDAQ 13:12:05 ET  OPEN · CLOSES IN 02:47:55
    const auto m = market_clock::read(now);
    if (!m.valid) {
        // This system has no New York time zone: no clock rather than a wrong one.
        market_label_->clear();
        market_label_->setToolTip(QStringLiteral("The market clock needs the America/New_York time zone, "
                                                 "which this system does not have."));
    }
    QString state;
    switch (m.session) {
    case market_clock::Session::Open:
        state = word(QStringLiteral("OPEN"), colors::POSITIVE()) +
                tag(m.early_close ? QStringLiteral(" · EARLY CLOSE IN ") : QStringLiteral(" · CLOSES IN "));
        break;
    case market_clock::Session::PreOpen:
        state = word(QStringLiteral("PRE-OPEN"), colors::AMBER()) + tag(QStringLiteral(" · OPENS IN "));
        break;
    case market_clock::Session::Closed:
        state = word(QStringLiteral("CLOSED"), colors::NEGATIVE()) + tag(QStringLiteral(" · OPENS IN "));
        break;
    }
    if (m.valid)
        market_label_->setText(tag(QStringLiteral("NYSE·NASDAQ ")) + m.et.toString(QStringLiteral("HH:mm:ss")) +
                               tag(QStringLiteral(" ET  ")) + state + market_clock::countdown(m.secs_to_event));

    // LOCAL 23 NOV 26 14:12:05 PST — the zone's abbreviation where the system
    // has one ("PST"); Windows gives long names ("Pacific Standard Time"): an
    // offset instead.
    QString zone = now.timeZoneAbbreviation();
    if (zone.size() > 5 || zone.contains(QLatin1Char(' '))) {
        const int off = now.offsetFromUtc() / 60;
        zone = QStringLiteral("UTC%1%2:%3")
                   .arg(off < 0 ? QStringLiteral("−") : QStringLiteral("+"))
                   .arg(std::abs(off) / 60, 2, 10, QLatin1Char('0'))
                   .arg(std::abs(off) % 60, 2, 10, QLatin1Char('0'));
    }
    clock_label_->setText(tag(QStringLiteral("LOCAL ")) + now.toString(QStringLiteral("dd MMM yy")).toUpper() +
                          QStringLiteral(" ") + now.toString(QStringLiteral("HH:mm:ss")) +
                          tag(QStringLiteral(" ") + zone));
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
