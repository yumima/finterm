// src/screens/pre_ipo/views/PicksView.cpp
#include "screens/pre_ipo/views/PicksView.h"

#include "ui/theme/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

PicksView::PicksView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PicksView::build_ui() {
    setObjectName("preIpoPicks");
    setStyleSheet(QString("#preIpoPicks{background:%1;}").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // ── Header ────────────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        header_lbl_ = new QLabel("DAILY PICKS");
        header_lbl_->setStyleSheet(
            QString("color:%1;font-size:14px;font-weight:800;letter-spacing:1.5px;background:transparent;")
                .arg(colors::AMBER()));
        row->addWidget(header_lbl_);

        auto* sub = new QLabel("Today's investable signals from the private market");
        sub->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
        row->addWidget(sub);
        row->addStretch();
        root->addLayout(row);
    }

    // Split: left = Top Picks (ranked), right = Today's Signals (event feed).
    auto* split = new QHBoxLayout;
    split->setSpacing(16);

    // Left half — Top Picks
    {
        auto* col = new QWidget;
        col->setStyleSheet(
            QString("background:%1;border-radius:4px;border:1px solid %2;")
                .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
        auto* cl = new QVBoxLayout(col);
        cl->setContentsMargins(12, 10, 12, 10);
        cl->setSpacing(6);

        auto* hr = new QHBoxLayout;
        auto* h = new QLabel("TOP PICKS");
        h->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        hr->addWidget(h);
        hr->addStretch();
        picks_count_lbl_ = new QLabel("0");
        picks_count_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(colors::AMBER()));
        hr->addWidget(picks_count_lbl_);
        cl->addLayout(hr);

        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");
        auto* host = new QWidget;
        host->setStyleSheet("background:transparent;");
        picks_layout_ = new QVBoxLayout(host);
        picks_layout_->setContentsMargins(0, 0, 0, 0);
        picks_layout_->setSpacing(6);
        picks_layout_->addStretch();
        scroll->setWidget(host);
        cl->addWidget(scroll, 1);

        split->addWidget(col, 1);
    }

    // Right half — Signals
    {
        auto* col = new QWidget;
        col->setStyleSheet(
            QString("background:%1;border-radius:4px;border:1px solid %2;")
                .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
        auto* cl = new QVBoxLayout(col);
        cl->setContentsMargins(12, 10, 12, 10);
        cl->setSpacing(6);

        auto* hr = new QHBoxLayout;
        auto* h = new QLabel("RECENT SIGNALS");
        h->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;letter-spacing:1px;background:transparent;")
                .arg(colors::TEXT_SECONDARY()));
        hr->addWidget(h);
        hr->addStretch();
        signals_count_lbl_ = new QLabel("0");
        signals_count_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(colors::CYAN()));
        hr->addWidget(signals_count_lbl_);
        cl->addLayout(hr);

        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");
        auto* host = new QWidget;
        host->setStyleSheet("background:transparent;");
        signals_layout_ = new QVBoxLayout(host);
        signals_layout_->setContentsMargins(0, 0, 0, 0);
        signals_layout_->setSpacing(4);
        signals_layout_->addStretch();
        scroll->setWidget(host);
        cl->addWidget(scroll, 1);

        split->addWidget(col, 1);
    }

    root->addLayout(split, 1);
}

bool PicksView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            auto* obj = w->property("_overlay").value<QObject*>();
            if (auto* btn = qobject_cast<QPushButton*>(obj))
                btn->setGeometry(w->rect());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PicksView::set_summary(const PreIpoSummary& summary) {
    // ── Clear ─────────────────────────────────────────────────────────────────
    while (picks_layout_->count() > 1) {
        auto* item = picks_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while (signals_layout_->count() > 1) {
        auto* item = signals_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // ── Top picks: top-10 by composite score ─────────────────────────────────
    QVector<PrivateCompany> ranked = summary.companies;
    std::sort(ranked.begin(), ranked.end(),
              [](const PrivateCompany& a, const PrivateCompany& b) {
                  return a.analytics.composite_picks_score > b.analytics.composite_picks_score;
              });
    int inserted = 0;
    for (const auto& c : ranked) {
        if (c.analytics.composite_picks_score <= 0 && c.cumulative_raised_m < 50) continue;
        picks_layout_->insertWidget(inserted++, make_pick_card(c));
        if (inserted >= 12) break;
    }
    picks_count_lbl_->setText(QString::number(inserted));

    // ── Signals: top 30 most recent ──────────────────────────────────────────
    int s_inserted = 0;
    for (const auto& s : summary.signal_list) {
        signals_layout_->insertWidget(s_inserted++, make_signal_card(s));
        if (s_inserted >= 30) break;
    }
    // Match picks_count_lbl_ semantics (count of cards actually rendered)
    // rather than the universe-wide signal total. The two badges were
    // inconsistent — picks showed inserted, signals showed total.
    signals_count_lbl_->setText(QString::number(s_inserted));
}

QWidget* PicksView::make_pick_card(const PrivateCompany& c) const {
    auto* card = new QWidget;
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(
        QString("QWidget{background:%1;border:1px solid %2;border-radius:4px;}"
                "QWidget:hover{border-color:%3;}")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM(), colors::AMBER()));
    card->setFixedHeight(64);

    auto* hl = new QHBoxLayout(card);
    hl->setContentsMargins(10, 6, 10, 6);
    hl->setSpacing(8);

    // Score badge
    auto* score = new QLabel(QString::number(c.analytics.ipo_readiness_score));
    score->setFixedSize(40, 40);
    score->setAlignment(Qt::AlignCenter);
    score->setStyleSheet(
        QString("color:%1;font-size:16px;font-weight:800;"
                "background:rgba(217,119,6,0.15);border:1px solid %1;border-radius:20px;")
            .arg(colors::AMBER()));
    hl->addWidget(score);

    // Center: name + sector
    auto* mid = new QVBoxLayout;
    mid->setSpacing(2);
    auto* name = new QLabel(c.name);
    name->setStyleSheet(
        QString("color:%1;font-size:13px;font-weight:700;background:transparent;").arg(colors::CYAN()));
    mid->addWidget(name);
    QString sub = c.sector;
    if (!c.hq_state.isEmpty()) sub += " · " + c.hq_state;
    if (c.cumulative_raised_m > 0)
        sub += QString(" · $%1M raised").arg(qRound(c.cumulative_raised_m));
    auto* sl = new QLabel(sub);
    sl->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    mid->addWidget(sl);
    hl->addLayout(mid, 1);

    // Right: composite score + drift
    auto* right = new QVBoxLayout;
    right->setSpacing(2);
    right->setAlignment(Qt::AlignRight);
    auto* comp = new QLabel(QString("%1").arg(c.analytics.composite_picks_score, 0, 'f', 1));
    comp->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;font-family:Consolas,monospace;background:transparent;")
            .arg(colors::TEXT_PRIMARY()));
    comp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    right->addWidget(comp);

    QString drift_txt = "—";
    QString drift_color = colors::TEXT_SECONDARY();
    if (c.analytics.mark_drift_vs_last_round_pct != 0) {
        const double v = c.analytics.mark_drift_vs_last_round_pct;
        drift_txt = (v >= 0 ? "+" : "") + QString::number(v, 'f', 1) + "%";
        drift_color = v >= 0 ? colors::POSITIVE() : colors::NEGATIVE();
    }
    auto* drift = new QLabel(drift_txt);
    drift->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(drift_color));
    drift->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    right->addWidget(drift);
    hl->addLayout(right);

    // Click handler
    auto* overlay = new QPushButton(card);
    overlay->setFlat(true);
    overlay->setStyleSheet("QPushButton{background:transparent;border:none;}");
    overlay->setGeometry(card->rect());
    overlay->raise();
    overlay->show();
    card->installEventFilter(const_cast<PicksView*>(this));
    const QString cid = c.id;
    auto* self = const_cast<PicksView*>(this);
    QObject::connect(overlay, &QPushButton::clicked, self, [self, cid] {
        emit self->company_selected(cid);
    });
    card->setProperty("_overlay", QVariant::fromValue(static_cast<QObject*>(overlay)));

    return card;
}

QWidget* PicksView::make_signal_card(const Signal& s) const {
    QString icon = "•";
    QString color = colors::TEXT_SECONDARY();
    switch (s.kind) {
        case SignalKind::MarkUp:          icon = "▲"; color = colors::POSITIVE(); break;
        case SignalKind::MarkDown:        icon = "▼"; color = colors::NEGATIVE(); break;
        case SignalKind::NewFiling:       icon = "+"; color = colors::CYAN(); break;
        case SignalKind::AmendmentBurst:  icon = "≡"; color = colors::AMBER(); break;
        case SignalKind::PremiumHigh:     icon = "$"; color = colors::POSITIVE(); break;
        case SignalKind::ReadinessJump:   icon = "★"; color = colors::AMBER(); break;
        case SignalKind::RoundFiled:      icon = "◆"; color = colors::CYAN(); break;
    }

    auto* row = new QWidget;
    row->setFixedHeight(40);
    row->setStyleSheet(
        QString("QWidget{background:%1;border:1px solid %2;border-radius:3px;}"
                "QWidget:hover{background:%3;}")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM(), colors::BG_BASE()));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(10, 6, 10, 6);
    hl->setSpacing(8);

    auto* ic = new QLabel(icon);
    ic->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:700;background:transparent;").arg(color));
    ic->setFixedWidth(16);
    hl->addWidget(ic);

    auto* name = new QLabel(s.company_name);
    name->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(colors::CYAN()));
    hl->addWidget(name);

    auto* desc = new QLabel(s.description);
    desc->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_PRIMARY()));
    hl->addWidget(desc, 1);

    auto* overlay = new QPushButton(row);
    overlay->setFlat(true);
    overlay->setStyleSheet("QPushButton{background:transparent;border:none;}");
    overlay->setGeometry(row->rect());
    overlay->raise();
    overlay->show();
    row->installEventFilter(const_cast<PicksView*>(this));
    const QString cid = s.company_id;
    auto* self = const_cast<PicksView*>(this);
    QObject::connect(overlay, &QPushButton::clicked, self, [self, cid] {
        if (!cid.isEmpty()) emit self->company_selected(cid);
    });
    row->setProperty("_overlay", QVariant::fromValue(static_cast<QObject*>(overlay)));

    return row;
}

} // namespace fincept::screens
