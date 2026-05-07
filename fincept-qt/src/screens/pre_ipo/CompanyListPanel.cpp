// src/screens/pre_ipo/CompanyListPanel.cpp
#include "screens/pre_ipo/CompanyListPanel.h"

#include "ui/theme/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

// ── Constructor ───────────────────────────────────────────────────────────────

CompanyListPanel::CompanyListPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(240);
    setMaximumWidth(340);
    build_ui();
}

// ── Public API ────────────────────────────────────────────────────────────────

void CompanyListPanel::set_companies(const QVector<PrivateCompany>& companies) {
    all_companies_ = companies;
    rebuild_list();
}

void CompanyListPanel::set_selected_id(const QString& id) {
    selected_id_ = id;
    rebuild_list();
}

// ── Build UI ─────────────────────────────────────────────────────────────────

void CompanyListPanel::build_ui() {
    setObjectName("companyListPanel");
    setStyleSheet(
        QString("#companyListPanel { background:%1; border-right:1px solid %2; }")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Title bar ─────────────────────────────────────────────────────────────
    auto* title_bar = new QWidget;
    title_bar->setFixedHeight(38);
    title_bar->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(colors::BG_RAISED(), colors::BORDER_DIM()));
    auto* tb_layout = new QHBoxLayout(title_bar);
    tb_layout->setContentsMargins(12, 0, 12, 0);
    auto* title_lbl = new QLabel("COMPANIES");
    title_lbl->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:700; letter-spacing:1.5px; background:transparent;")
            .arg(colors::TEXT_SECONDARY()));
    tb_layout->addWidget(title_lbl);
    tb_layout->addStretch();
    root->addWidget(title_bar);

    // ── Search bar ────────────────────────────────────────────────────────────
    auto* search_wrapper = new QWidget;
    search_wrapper->setFixedHeight(38);
    search_wrapper->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    auto* sw_layout = new QHBoxLayout(search_wrapper);
    sw_layout->setContentsMargins(8, 6, 8, 6);

    search_edit_ = new QLineEdit;
    search_edit_->setPlaceholderText("Search companies…");
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setStyleSheet(
        QString("QLineEdit { background:%1; color:%2; border:1px solid %3;"
                "  border-radius:3px; padding:3px 8px; font-size:11px; }"
                "QLineEdit:focus { border-color:%4; }")
            .arg(colors::BG_RAISED(), colors::TEXT_PRIMARY(),
                 colors::BORDER_DIM(), colors::BORDER_MED()));
    sw_layout->addWidget(search_edit_);
    root->addWidget(search_wrapper);

    connect(search_edit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        search_text_ = t;
        apply_filter();
    });

    // ── Filter chips ─────────────────────────────────────────────────────────
    filter_bar_ = new QWidget;
    filter_bar_->setFixedHeight(36);
    filter_bar_->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));

    auto* filter_scroll = new QScrollArea(filter_bar_);
    filter_scroll->setWidgetResizable(true);
    filter_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filter_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filter_scroll->setFrameShape(QFrame::NoFrame);
    filter_scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }");

    auto* chips_bar_layout = new QHBoxLayout(filter_bar_);
    chips_bar_layout->setContentsMargins(0, 0, 0, 0);
    chips_bar_layout->setSpacing(0);
    chips_bar_layout->addWidget(filter_scroll);

    auto* chips_host = new QWidget;
    chips_host->setStyleSheet("background:transparent;");
    filter_layout_ = new QHBoxLayout(chips_host);
    filter_layout_->setContentsMargins(6, 4, 6, 4);
    filter_layout_->setSpacing(4);
    filter_scroll->setWidget(chips_host);

    const QVector<QPair<QString, QString>> chips = {
        {"All", "all"},
        {"Watched", "watched"},
        {"Filed", "filed"},
        {"AI", "ai"},
        {"Fintech", "fintech"},
        {"Defense", "defense"},
    };

    for (const auto& [label, tag] : chips) {
        auto* btn = new QPushButton(label);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);
        btn->setStyleSheet(
            QString("QPushButton { background:transparent; color:%1; border:1px solid %2;"
                    "  border-radius:11px; padding:0 8px; font-size:12px; font-weight:600; }"
                    "QPushButton:checked { background:%3; color:%4; border-color:%3; }"
                    "QPushButton:hover:!checked { color:%5; border-color:%5; }")
                .arg(colors::TEXT_TERTIARY(), colors::BORDER_DIM(),
                     colors::AMBER(), colors::BG_BASE(), colors::TEXT_PRIMARY()));

        const QString t = tag;
        connect(btn, &QPushButton::clicked, this, [this, t]() {
            set_active_filter(t);
        });

        filter_btns_.append(btn);
        filter_layout_->addWidget(btn);
    }
    filter_layout_->addStretch();
    root->addWidget(filter_bar_);

    // Set "All" as default
    if (!filter_btns_.isEmpty()) {
        filter_btns_.first()->setChecked(true);
        active_filter_ = "all";
    }

    // ── Scrollable company list ───────────────────────────────────────────────
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet(
        QString("QScrollArea { border:none; background:%1; }"
                "QScrollBar:vertical { width:4px; background:transparent; }"
                "QScrollBar::handle:vertical { background:%2; border-radius:2px; min-height:20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
            .arg(colors::BG_SURFACE(), colors::BORDER_MED()));

    list_container_ = new QWidget;
    list_container_->setStyleSheet(
        QString("background:%1;").arg(colors::BG_SURFACE()));
    list_layout_ = new QVBoxLayout(list_container_);
    list_layout_->setContentsMargins(6, 6, 6, 6);
    list_layout_->setSpacing(4);
    list_layout_->addStretch();

    scroll_->setWidget(list_container_);
    root->addWidget(scroll_, 1);
}

// ── List building ─────────────────────────────────────────────────────────────

void CompanyListPanel::rebuild_list() {
    // Clear existing cards (but not the trailing stretch)
    while (list_layout_->count() > 1) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    apply_filter();
}

void CompanyListPanel::apply_filter() {
    // Remove all cards except the trailing stretch
    while (list_layout_->count() > 1) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    int inserted = 0;
    for (const auto& c : all_companies_) {
        // Search filter
        if (!search_text_.isEmpty()) {
            const QString lo = search_text_.toLower();
            if (!c.name.toLower().contains(lo) &&
                !c.sector.toLower().contains(lo) &&
                !c.hq_city.toLower().contains(lo))
                continue;
        }

        // Tag / chip filter
        if (active_filter_ == "watched" && !c.watched)
            continue;
        if (active_filter_ == "filed" && c.ipo_status != IpoStatus::Filed)
            continue;
        if (active_filter_ == "ai" && !c.tags.contains("ai"))
            continue;
        if (active_filter_ == "fintech" && !c.tags.contains("fintech"))
            continue;
        if (active_filter_ == "defense" && !c.tags.contains("defense"))
            continue;

        auto* card = make_card(c);
        list_layout_->insertWidget(inserted, card);
        inserted++;
    }

    if (inserted == 0) {
        auto* empty = new QLabel("No companies match");
        empty->setStyleSheet(
            QString("color:%1; font-size:11px; padding:20px;").arg(colors::TEXT_TERTIARY()));
        empty->setAlignment(Qt::AlignCenter);
        list_layout_->insertWidget(0, empty);
    }
}

void CompanyListPanel::set_active_filter(const QString& tag) {
    active_filter_ = tag;
    for (int i = 0; i < filter_btns_.size(); ++i) {
        const QVector<QString> tags = {"all","watched","filed","ai","fintech","defense"};
        filter_btns_[i]->setChecked(i < tags.size() && tags[i] == tag);
    }
    apply_filter();
}

// ── Event filter ──────────────────────────────────────────────────────────────

bool CompanyListPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        auto* card = qobject_cast<QWidget*>(obj);
        if (card) {
            QObject* ovr = card->property("overlay_btn").value<QObject*>();
            if (auto* btn = qobject_cast<QPushButton*>(ovr))
                btn->setGeometry(card->rect());
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── Card factory ──────────────────────────────────────────────────────────────

QWidget* CompanyListPanel::make_card(const PrivateCompany& c) const {
    const bool is_selected = (c.id == selected_id_);

    auto* card = new QWidget;
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedHeight(62);

    const QString bg  = is_selected ? "rgba(217,119,6,0.12)" : colors::BG_RAISED();
    const QString border = is_selected ? colors::AMBER() : colors::BORDER_DIM();

    card->setStyleSheet(
        QString("QWidget { background:%1; border:1px solid %2; border-radius:4px; }"
                "QWidget:hover { background:rgba(255,255,255,0.04); border-color:%3; }")
            .arg(bg, border, colors::BORDER_MED()));

    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(10, 7, 10, 7);
    card_layout->setSpacing(3);

    // Row 1: name + valuation badge
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(6);

    auto* name_lbl = new QLabel(c.name);
    name_lbl->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; background:transparent;")
            .arg(colors::CYAN()));
    row1->addWidget(name_lbl, 1);

    // Valuation badge
    if (c.last_valuation_usd > 0) {
        auto* val_badge = new QLabel(valuation_label(c.last_valuation_usd));
        val_badge->setStyleSheet(
            QString("color:%1; font-size:11px; font-weight:700;"
                    "  background:rgba(217,119,6,0.15); border:1px solid rgba(217,119,6,0.3);"
                    "  border-radius:3px; padding:1px 5px;")
                .arg(colors::AMBER()));
        val_badge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row1->addWidget(val_badge);
    }
    card_layout->addLayout(row1);

    // Row 2: sector + IPO status badge
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(6);

    auto* sector_lbl = new QLabel(c.sector);
    sector_lbl->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_SECONDARY()));
    row2->addWidget(sector_lbl, 1);

    row2->addWidget(make_status_badge(c.ipo_status, card));
    card_layout->addLayout(row2);

    // Click handler — transparent push button that fills the card via an absolute layout
    const QString cid = c.id;
    auto* signal_sender = const_cast<CompanyListPanel*>(this);

    auto* overlay = new QPushButton(card);
    overlay->setFlat(true);
    overlay->setCursor(Qt::PointingHandCursor);
    overlay->setStyleSheet("QPushButton { background:transparent; border:none; }");
    // Size the overlay to exactly cover the card; updated by resizeEvent via event filter
    overlay->setGeometry(card->rect());
    overlay->raise();

    // Keep overlay geometry in sync when card changes size
    card->installEventFilter(signal_sender);

    // Store the overlay pointer in the card's dynamic property so the event filter can find it
    card->setProperty("overlay_btn", QVariant::fromValue(static_cast<QObject*>(overlay)));

    QObject::connect(overlay, &QPushButton::clicked, signal_sender, [signal_sender, cid]() {
        emit signal_sender->company_selected(cid);
    });

    return card;
}

// static
QWidget* CompanyListPanel::make_status_badge(IpoStatus status, QWidget* parent) {
    QString label;
    QString bg, fg;

    switch (status) {
        case IpoStatus::Filed:
            label = "Filed"; bg = "rgba(217,119,6,0.2)"; fg = colors::AMBER();
            break;
        case IpoStatus::Rumored:
            label = "Rumored"; bg = "rgba(107,114,128,0.2)"; fg = colors::TEXT_TERTIARY();
            break;
        case IpoStatus::Priced:
            label = "Priced"; bg = "rgba(22,163,74,0.2)"; fg = colors::POSITIVE();
            break;
        case IpoStatus::Listed:
            label = "Listed"; bg = "rgba(22,163,74,0.2)"; fg = colors::POSITIVE();
            break;
        case IpoStatus::Acquired:
            label = "Acquired"; bg = "rgba(139,92,246,0.2)"; fg = "#a78bfa";
            break;
        default:
            return new QWidget(parent); // invisible placeholder
    }

    auto* badge = new QLabel(label, parent);
    badge->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:700; background:%2;"
                "  border-radius:3px; padding:1px 5px;")
            .arg(fg, bg));
    badge->setAlignment(Qt::AlignCenter);
    return badge;
}

// static
QString CompanyListPanel::valuation_label(double b) {
    if (b >= 100.0)
        return QString("$%1T").arg(b / 1000.0, 0, 'f', 1);
    if (b >= 1.0)
        return QString("$%1B").arg(b, 0, 'f', b >= 10 ? 0 : 1);
    return QString("$%1M").arg(b * 1000, 0, 'f', 0);
}

} // namespace fincept::screens
