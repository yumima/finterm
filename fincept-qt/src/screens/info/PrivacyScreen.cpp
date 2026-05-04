#include "screens/info/PrivacyScreen.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;

static const char* MF = "font-family:'Consolas','Courier New',monospace;";

static QLabel* section_heading(const QString& icon, const QString& title) {
    auto* lbl = new QLabel(QString("%1  %2").arg(icon, title));
    lbl->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: 700; "
                               "background: transparent; margin-top: 8px; %2")
                           .arg(colors::AMBER(), MF));
    return lbl;
}

static QLabel* body_text(const QString& text) {
    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("color: %1; font-size: 12px; line-height: 165%%; background: transparent; %2")
                           .arg(colors::TEXT_PRIMARY(), MF));
    return lbl;
}

static QLabel* bullet(const QString& text) {
    auto* lbl = new QLabel(QString("  > %1").arg(text));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(
        QString("color: %1; font-size: 12px; background: transparent; %2").arg(colors::TEXT_SECONDARY(), MF));
    return lbl;
}

PrivacyScreen::PrivacyScreen(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("QWidget#PrivacyRoot { background: %1; }").arg(colors::BG_BASE()));
    setObjectName("PrivacyRoot");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* page = new QWidget(this);
    page->setStyleSheet(QString("background: %1;").arg(colors::BG_BASE()));
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(24, 24, 24, 24);
    vl->setSpacing(6);

    // Back
    auto* back_btn = new QPushButton("< BACK");
    back_btn->setCursor(Qt::PointingHandCursor);
    back_btn->setStyleSheet(QString("QPushButton { color: %1; background: transparent; border: none; "
                                    "font-size: 12px; %2 } QPushButton:hover { color: %3; }")
                                .arg(colors::TEXT_SECONDARY(), MF, colors::TEXT_PRIMARY()));
    connect(back_btn, &QPushButton::clicked, this, &PrivacyScreen::navigate_back);
    vl->addWidget(back_btn, 0, Qt::AlignLeft);

    auto* title = new QLabel("PRIVACY");
    title->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: 700; letter-spacing: 1px; "
                                 "background: transparent; %2")
                             .arg(colors::AMBER(), MF));
    vl->addWidget(title);

    auto* subtitle = new QLabel("Local-first by design. No accounts, no telemetry, no cloud.");
    subtitle->setStyleSheet(
        QString("color: %1; font-size: 12px; background: transparent; %2").arg(colors::TEXT_TERTIARY(), MF));
    subtitle->setWordWrap(true);
    vl->addWidget(subtitle);
    vl->addSpacing(12);

    // Main panel
    auto* panel = new QWidget(this);
    panel->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 2px;")
                             .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    auto* pvl = new QVBoxLayout(panel);
    pvl->setContentsMargins(20, 16, 20, 16);
    pvl->setSpacing(6);

    // 1 — TL;DR
    pvl->addWidget(section_heading("#", "TL;DR"));
    pvl->addWidget(body_text(
        "finterm runs entirely on your machine. There is no SaaS account, no cloud round-trips, no telemetry, "
        "no analytics, no crash reporter. The project does not collect any data about you or what you do with the app. "
        "The only outbound network traffic is the public market-data APIs you explicitly use."));

    // 2 — What stays local
    pvl->addWidget(section_heading("@", "WHAT STAYS LOCAL"));
    pvl->addWidget(body_text("Stored only on your machine, in plain SQLite / JSON files:"));
    pvl->addWidget(bullet("Portfolio holdings, transactions, P&L history"));
    pvl->addWidget(bullet("Watchlists, alerts, custom indices"));
    pvl->addWidget(bullet("Notes, chat history with any LLM you wired up"));
    pvl->addWidget(bullet("Window layouts, theme, workspaces"));
    pvl->addWidget(bullet("Cached candles, quotes, news"));
    pvl->addWidget(bullet("Login state for the localhost stub"));

    pvl->addSpacing(4);
    pvl->addWidget(body_text("Locations: ~/.local/share/com.fincept.terminal/ (data) and ~/.config/Fincept/ (settings)."));

    // 3 — Outbound
    pvl->addWidget(section_heading("→", "OUTBOUND TRAFFIC"));
    pvl->addWidget(body_text("On your behalf — fetched into the app, never sent out:"));
    pvl->addWidget(bullet("Yahoo Finance — quotes, history, fundamentals (yfinance)"));
    pvl->addWidget(bullet("Stooq — optional EOD data"));
    pvl->addWidget(bullet("akshare — Chinese exchange data (only when you use the China futures panel)"));
    pvl->addWidget(bullet("FRED, DBnomics, government data sources — when you query them"));

    pvl->addSpacing(4);
    pvl->addWidget(body_text("Never:"));
    pvl->addWidget(bullet("No analytics provider"));
    pvl->addWidget(bullet("No crash reporter"));
    pvl->addWidget(bullet("No SaaS backend (auth/profile flows go to a localhost stub on 127.0.0.1:8765)"));
    pvl->addWidget(bullet("No telemetry of any kind"));

    // 4 — LLM opt-in
    pvl->addWidget(section_heading("∗", "LLM PROVIDER (OPT-IN)"));
    pvl->addWidget(body_text(
        "If you configure an external LLM in Settings → Agent (OpenAI / Anthropic / local Ollama / etc.), "
        "your prompts and responses go to whatever endpoint you configured. That is the one and only outbound "
        "channel under your direct control. No LLM is wired by default."));

    // 5 — Removing your data
    pvl->addWidget(section_heading("✕", "REMOVING YOUR DATA"));
    pvl->addWidget(body_text(
        "Because everything is local, removing your data is `rm -rf` on the data + config directories listed "
        "above. There is nothing on a server to delete. `finterm reset --full` does this with a backup, restorable "
        "via `finterm reset --restore <ts>`."));

    // 6 — Source code
    pvl->addWidget(section_heading("§", "INSPECT THE SOURCE"));
    pvl->addWidget(body_text(
        "AGPL-3.0. Every line of network code in this repository is open. If you want to verify these claims, "
        "the data layer is in fincept-qt/scripts/ and the localhost stub is in tools/local_stub/server.py."));

    vl->addWidget(panel);

    // Footer navigation
    auto* footer = new QWidget(this);
    footer->setStyleSheet("background: transparent;");
    auto* fhl = new QHBoxLayout(footer);
    fhl->setContentsMargins(0, 12, 0, 0);

    auto make_link = [](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton { color: %1; background: transparent; border: none; "
                                   "font-size: 12px; font-family:'Consolas','Courier New',monospace; }"
                                   "QPushButton:hover { color: #38bdf8; }")
                               .arg(colors::CYAN()));
        return btn;
    };

    auto* terms_link = make_link("Terms");
    connect(terms_link, &QPushButton::clicked, this, &PrivacyScreen::navigate_terms);
    fhl->addWidget(terms_link);

    fhl->addStretch();

    auto* contact_link = make_link("Contact");
    connect(contact_link, &QPushButton::clicked, this, &PrivacyScreen::navigate_contact);
    fhl->addWidget(contact_link);

    vl->addWidget(footer);
    vl->addStretch();

    scroll->setWidget(page);
    root->addWidget(scroll, 1);
}

} // namespace fincept::screens
