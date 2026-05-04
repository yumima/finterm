#include "screens/info/TermsScreen.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;

static const char* MF = "font-family:'Consolas','Courier New',monospace;";

static QLabel* section_heading(const QString& number, const QString& title) {
    auto* lbl = new QLabel(QString("%1. %2").arg(number, title));
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

TermsScreen::TermsScreen(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("QWidget#TermsRoot { background: %1; }").arg(colors::BG_BASE()));
    setObjectName("TermsRoot");

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
    connect(back_btn, &QPushButton::clicked, this, &TermsScreen::navigate_back);
    vl->addWidget(back_btn, 0, Qt::AlignLeft);

    auto* title = new QLabel("TERMS");
    title->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: 700; letter-spacing: 1px; "
                                 "background: transparent; %2")
                             .arg(colors::AMBER(), MF));
    vl->addWidget(title);

    auto* subtitle = new QLabel("Open-source software under AGPL-3.0. Use at your own risk.");
    subtitle->setStyleSheet(
        QString("color: %1; font-size: 12px; background: transparent; %2").arg(colors::TEXT_TERTIARY(), MF));
    subtitle->setWordWrap(true);
    vl->addWidget(subtitle);
    vl->addSpacing(12);

    // Panel
    auto* panel = new QWidget(this);
    panel->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 2px;")
                             .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    auto* pvl = new QVBoxLayout(panel);
    pvl->setContentsMargins(20, 16, 20, 16);
    pvl->setSpacing(6);

    pvl->addWidget(section_heading("1", "LICENSE"));
    pvl->addWidget(body_text(
        "finterm is licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0). "
        "The full license text is in the LICENSE file in the repository root and at "
        "https://www.gnu.org/licenses/agpl-3.0.html. By using, modifying, or distributing this software you "
        "agree to comply with the license."));

    pvl->addWidget(section_heading("2", "NO WARRANTY"));
    pvl->addWidget(body_text(
        "This software is provided \"AS IS\", without warranty of any kind, express or implied, including "
        "but not limited to the warranties of merchantability, fitness for a particular purpose, and non-infringement. "
        "In no event shall the authors or copyright holders be liable for any claim, damages, or other liability "
        "arising from the use of the software."));

    pvl->addWidget(section_heading("3", "NOT FINANCIAL ADVICE"));
    pvl->addWidget(body_text(
        "All market data, fundamentals, technical indicators, AI-generated commentary, and analytics displayed "
        "by this software are for informational and educational purposes only. Nothing in this software constitutes "
        "investment, legal, or tax advice. Do your own research; consult a licensed professional before making "
        "financial decisions."));
    pvl->addWidget(bullet("Market data may be delayed, incomplete, or wrong"));
    pvl->addWidget(bullet("Backtests are historical and not predictive"));
    pvl->addWidget(bullet("AI outputs may be inaccurate or fabricated"));
    pvl->addWidget(bullet("You bear full responsibility for any trades you place"));

    pvl->addWidget(section_heading("4", "DATA SOURCES"));
    pvl->addWidget(body_text(
        "Market data is fetched on your behalf from public APIs (Yahoo Finance, Stooq, akshare, FRED, etc.) "
        "subject to those providers' own terms of service. You are responsible for complying with each provider's "
        "rate limits and usage rules."));

    pvl->addWidget(section_heading("5", "NO COMMERCIAL SUPPORT"));
    pvl->addWidget(body_text(
        "This is a community open-source project. There is no commercial support, no SLA, no help desk, no "
        "guaranteed response time. Use GitHub issues / discussions for community-level help."));

    pvl->addWidget(section_heading("6", "ACCEPTABLE USE"));
    pvl->addWidget(body_text("You agree to:"));
    pvl->addWidget(bullet("Comply with all applicable laws and regulations in your jurisdiction"));
    pvl->addWidget(bullet("Respect upstream API terms (rate limits, usage caps, etc.)"));
    pvl->addWidget(bullet("Not use the software to harm others or as part of unauthorized access systems"));
    pvl->addWidget(bullet("Comply with the AGPL-3.0 source-disclosure requirement when modifying and distributing"));

    pvl->addWidget(section_heading("7", "MODIFICATIONS TO TERMS"));
    pvl->addWidget(body_text(
        "These terms may be updated. The current version always lives in the source tree. Use of any new release "
        "after an update implies acceptance of the new terms."));

    vl->addWidget(panel);

    // Footer
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

    auto* privacy_link = make_link("Privacy");
    connect(privacy_link, &QPushButton::clicked, this, &TermsScreen::navigate_privacy);
    fhl->addWidget(privacy_link);

    fhl->addStretch();

    auto* contact_link = make_link("Contact");
    connect(contact_link, &QPushButton::clicked, this, &TermsScreen::navigate_contact);
    fhl->addWidget(contact_link);

    vl->addWidget(footer);
    vl->addStretch();

    scroll->setWidget(page);
    root->addWidget(scroll, 1);
}

} // namespace fincept::screens
