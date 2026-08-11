// src/screens/portfolio/PortfolioStatusBar.cpp
#include "screens/portfolio/PortfolioStatusBar.h"

#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QTimeZone>

namespace fincept::screens {

PortfolioStatusBar::PortfolioStatusBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(24);
    setObjectName("portfolioStatusBar");
    setStyleSheet(QString("#portfolioStatusBar { background:%1; border-top:1px solid %2; }")
                      .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(0);

    auto make_label = [&](const char* color, bool bold = false) {
        auto* lbl = new QLabel;
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:%2; letter-spacing:0.3px;")
                               .arg(color)
                               .arg(bold ? 700 : 400));
        layout->addWidget(lbl);
        return lbl;
    };

    auto add_divider = [&]() {
        auto* sep = new QLabel(" | ");
        sep->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::BORDER_MED()));
        layout->addWidget(sep);
    };

    // Left section
    brand_label_ = make_label(ui::colors::AMBER, true);
    brand_label_->setText("FINTERM");

    add_divider();

    auto* version_lbl = make_label(ui::colors::TEXT_TERTIARY);
    version_lbl->setText("PORTFOLIO TERMINAL v4.0");

    add_divider();

    portfolio_label_ = make_label(ui::colors::CYAN, true);
    portfolio_label_->setText("");

    add_divider();

    // Live indicator
    // Set from real state in set_summary(), not once at construction. A
    // permanently-green "● LIVE" is a connectivity indicator that cannot
    // indicate anything — it said LIVE with the network down.
    live_label_ = make_label(ui::colors::TEXT_SECONDARY, true);
    live_label_->setText(QStringLiteral("\u25CB —"));

    add_divider();

    positions_label_ = make_label(ui::colors::TEXT_SECONDARY);
    positions_label_->setText("0 positions");

    layout->addStretch();

    // Right section
    nav_label_ = make_label(ui::colors::WARNING, true);

    add_divider();

    pnl_label_ = make_label(ui::colors::TEXT_PRIMARY);

    add_divider();

    time_label_ = make_label(ui::colors::CYAN, true);

    tz_label_ = make_label(ui::colors::AMBER, true);

    // Clock timer (P3: don't start in constructor)
    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &PortfolioStatusBar::update_clock);
}

void PortfolioStatusBar::start_clock() {
    clock_timer_->start();
    update_clock();
}

void PortfolioStatusBar::stop_clock() {
    clock_timer_->stop();
}

void PortfolioStatusBar::update_clock() {
    auto now = QDateTime::currentDateTime();
    time_label_->setText(now.toString("hh:mm:ss"));
    tz_label_->setText(QString(" %1").arg(now.timeZone().abbreviation(now)));
}

void PortfolioStatusBar::set_portfolio_name(const QString& name) {
    portfolio_label_->setText(name.toUpper());
}

void PortfolioStatusBar::set_summary(const portfolio::PortfolioSummary& s) {
    auto fmt = [](double v) { return QString::number(v, 'f', 2); };

    set_portfolio_name(s.portfolio.name);
    positions_label_->setText(QString("%1 positions").arg(s.total_positions));

    // LIVE vs CACHED from the summary's own provenance: from_cache is set
    // when the numbers came off disk rather than a fresh computation.
    if (live_label_) {
        const bool live = !s.from_cache;
        live_label_->setText(live ? QStringLiteral("\u25CF LIVE") : QStringLiteral("\u25CB CACHED"));
        live_label_->setStyleSheet(
            QString("color:%1; font-weight:700;")
                .arg(live ? ui::colors::POSITIVE() : ui::colors::TEXT_SECONDARY()));
        live_label_->setToolTip(live
                                    ? QStringLiteral("Values computed from a fresh quote fetch.")
                                    : QStringLiteral("Values restored from the on-disk cache — a refresh\n"
                                                     "is in flight; they update when it lands."));
    }

    nav_label_->setText(QString("NAV %1 %2").arg(s.portfolio.currency, fmt(s.total_market_value)));

    double pnl = s.total_unrealized_pnl;
    pnl_label_->setText(QString("P&L %1%2").arg(pnl >= 0 ? "+" : "").arg(fmt(pnl)));
    pnl_label_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600;")
                                  .arg(pnl >= 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
}

} // namespace fincept::screens
