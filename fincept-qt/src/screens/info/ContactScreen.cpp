#include "screens/info/ContactScreen.h"

#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;

static QString PANEL() {
    return QString("background: %1; border: 1px solid %2; border-radius: 2px;")
        .arg(colors::BG_SURFACE(), colors::BORDER_DIM());
}

static const char* MF = "font-family:'Consolas','Courier New',monospace;";

static QLabel* make_header(const QString& icon, const QString& title, const QString& icon_color) {
    auto* lbl = new QLabel(QString("%1  %2").arg(icon, title));
    lbl->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold; letter-spacing: 0.5px; "
                               "background: %2; padding: 10px 14px; border-bottom: 1px solid %3; %4")
                           .arg(icon_color, colors::BG_RAISED(), colors::BORDER_DIM(), MF));
    return lbl;
}

ContactScreen::ContactScreen(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("QWidget#ContactRoot { background: %1; }").arg(colors::BG_BASE()));
    setObjectName("ContactRoot");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* page = new QWidget(this);
    page->setStyleSheet(QString("background: %1;").arg(colors::BG_BASE()));
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(24, 24, 24, 24);
    vl->setSpacing(12);

    // ── Header ───────────────────────────────────────────────────────────────
    auto* back_btn = new QPushButton("< BACK");
    back_btn->setCursor(Qt::PointingHandCursor);
    back_btn->setStyleSheet(QString("QPushButton { color: %1; background: transparent; border: none; "
                                    "font-size: 12px; %2 } QPushButton:hover { color: %3; }")
                                .arg(colors::TEXT_SECONDARY(), MF, colors::TEXT_PRIMARY()));
    connect(back_btn, &QPushButton::clicked, this, &ContactScreen::navigate_back);
    vl->addWidget(back_btn, 0, Qt::AlignLeft);

    auto* title = new QLabel("CONTACT");
    title->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: 700; letter-spacing: 1px; "
                                 "background: transparent; %2")
                             .arg(colors::AMBER(), MF));
    vl->addWidget(title);

    auto* subtitle = new QLabel("This is an open-source project. There is no commercial support.");
    subtitle->setStyleSheet(
        QString("color: %1; font-size: 13px; background: transparent; %2").arg(colors::TEXT_TERTIARY(), MF));
    subtitle->setWordWrap(true);
    vl->addWidget(subtitle);

    vl->addSpacing(8);

    // ── Where to go ──────────────────────────────────────────────────────────
    {
        auto* panel = new QWidget(this);
        panel->setStyleSheet(PANEL());
        auto* pvl = new QVBoxLayout(panel);
        pvl->setContentsMargins(0, 0, 0, 0);
        pvl->setSpacing(0);

        pvl->addWidget(make_header(">>", "WHERE TO GO", colors::AMBER));

        auto* body = new QWidget(this);
        body->setStyleSheet("background: transparent;");
        auto* bvl = new QVBoxLayout(body);
        bvl->setContentsMargins(14, 14, 14, 14);
        bvl->setSpacing(12);

        struct Channel {
            QString label;
            QString purpose;
            QString url;
        };
        const Channel channels[] = {
            {"GitHub Issues", "Bug reports, regressions, reproducible failures",
             "https://github.com/Fincept-Corporation/FinceptTerminal/issues"},
            {"GitHub Discussions", "Questions, feature ideas, general help",
             "https://github.com/Fincept-Corporation/FinceptTerminal/discussions"},
            {"Pull Requests", "Patches and improvements — welcome and reviewed",
             "https://github.com/Fincept-Corporation/FinceptTerminal/pulls"},
        };

        for (const auto& c : channels) {
            auto* row = new QWidget(body);
            row->setStyleSheet("background: transparent;");
            auto* rl = new QVBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(2);

            auto* btn = new QPushButton(QString("→  %1").arg(c.label));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QString("QPushButton { color: %1; background: transparent; border: none; text-align: left; "
                                       "padding: 0; font-size: 13px; font-weight: 600; %2 } "
                                       "QPushButton:hover { color: %3; }")
                                   .arg(colors::CYAN(), MF, colors::AMBER()));
            const QString url = c.url;
            connect(btn, &QPushButton::clicked, btn, [url]() { QDesktopServices::openUrl(QUrl(url)); });
            rl->addWidget(btn);

            auto* desc = new QLabel(c.purpose);
            desc->setStyleSheet(QString("color: %1; font-size: 11px; background: transparent; padding-left: 18px; %2")
                                    .arg(colors::TEXT_TERTIARY(), MF));
            desc->setWordWrap(true);
            rl->addWidget(desc);
            bvl->addWidget(row);
        }

        pvl->addWidget(body);
        vl->addWidget(panel);
    }

    // ── What this project is ─────────────────────────────────────────────────
    {
        auto* panel = new QWidget(this);
        panel->setStyleSheet(PANEL());
        auto* pvl = new QVBoxLayout(panel);
        pvl->setContentsMargins(0, 0, 0, 0);
        pvl->setSpacing(0);

        pvl->addWidget(make_header("?", "ABOUT THIS PROJECT", colors::CYAN));

        auto* body = new QLabel(
            "finterm is a community fork of an open-source terminal, distributed under AGPL-3.0. "
            "There is no commercial entity behind it, no email support, no phone, no SLA. "
            "The maintainer(s) review GitHub issues and PRs as time permits. "
            "Patches are the most reliable way to get something fixed.");
        body->setWordWrap(true);
        body->setStyleSheet(QString("color: %1; font-size: 12px; line-height: 165%%; background: transparent; "
                                    "padding: 14px; %2")
                                .arg(colors::TEXT_PRIMARY(), MF));
        pvl->addWidget(body);
        vl->addWidget(panel);
    }

    vl->addStretch();
    scroll->setWidget(page);
    root->addWidget(scroll, 1);
}

} // namespace fincept::screens
