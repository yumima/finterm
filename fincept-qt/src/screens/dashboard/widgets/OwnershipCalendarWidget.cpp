// src/screens/dashboard/widgets/OwnershipCalendarWidget.cpp
#include "screens/dashboard/widgets/OwnershipCalendarWidget.h"

#include "core/events/EventBus.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/TooltipText.h"
#include "ui/theme/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>

namespace fincept::screens::widgets {

using ownership::CalendarEntry;

namespace {
constexpr int kDays = 90;
constexpr const char* kFontPx = "12px";

QString kind_label(CalendarEntry::Kind k) {
    switch (k) {
        case CalendarEntry::Kind::Form13FDeadline: return QStringLiteral("13F due");
        case CalendarEntry::Kind::FinraPublication: return QStringLiteral("Short interest");
        case CalendarEntry::Kind::LockupExpiry: return QStringLiteral("Lock-up ends");
    }
    return {};
}

QString kind_colour(CalendarEntry::Kind k) {
    switch (k) {
        case CalendarEntry::Kind::Form13FDeadline: return ui::colors::AMBER();
        case CalendarEntry::Kind::FinraPublication: return ui::colors::CYAN();
        case CalendarEntry::Kind::LockupExpiry: return ui::colors::RED();
    }
    return ui::colors::TEXT_PRIMARY();
}
} // namespace

OwnershipCalendarWidget::OwnershipCalendarWidget(QWidget* parent)
    : BaseWidget(QStringLiteral("OWNERSHIP CALENDAR"), parent) {
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    header_widget_ = new QWidget;
    auto* h = new QHBoxLayout(header_widget_);
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(8);
    for (const auto& [text, stretch] : {std::pair{QStringLiteral("DATE"), 2}, std::pair{QStringLiteral("EVENT"), 3},
                                        std::pair{QStringLiteral("WHAT"), 5}}) {
        auto* l = new QLabel(text);
        header_labels_.push_back(l);
        h->addWidget(l, stretch);
    }
    v->addWidget(header_widget_);
    header_sep_ = new QFrame;
    header_sep_->setFixedHeight(1);
    v->addWidget(header_sep_);

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    auto* list = new QWidget;
    list_layout_ = new QVBoxLayout(list);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(0);
    list_layout_->addStretch(1);
    scroll_area_->setWidget(list);
    v->addWidget(scroll_area_, 1);

    status_label_ = new QLabel;
    status_label_->setWordWrap(true);
    v->addWidget(status_label_);
    content_layout()->addWidget(body, 1);

    connect(this, &BaseWidget::refresh_requested, this, [this]() { refresh_data(); });
    connect(&services::OwnershipService::instance(), &services::OwnershipService::calendar_updated, this,
            [this]() { populate(); });
    apply_styles();
    refresh_data();
}

void OwnershipCalendarWidget::on_theme_changed() { apply_styles(); }

bool OwnershipCalendarWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            const QString sym = w->property("symbol").toString();
            if (!sym.isEmpty()) {
                // The same two events the shell uses to open a stock: raise
                // Equity Research, then hand it the symbol. Order matters —
                // the screen listens only once it has been materialised.
                EventBus::instance().publish(QStringLiteral("nav.switch_screen"),
                                             QVariantMap{{QStringLiteral("screen_id"), QStringLiteral("equity_research")}});
                EventBus::instance().publish(QStringLiteral("equity_research.load_symbol"),
                                             QVariantMap{{QStringLiteral("symbol"), sym}});
                return true;
            }
        }
    }
    return BaseWidget::eventFilter(watched, event);
}

void OwnershipCalendarWidget::apply_styles() {
    header_widget_->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_RAISED()));
    for (auto* l : header_labels_)
        l->setStyleSheet(QString("color: %1; font-weight: bold; background: transparent; font-size:%2;")
                             .arg(ui::colors::TEXT_SECONDARY(), kFontPx));
    header_sep_->setStyleSheet(QString("background: %1;").arg(ui::colors::BORDER_DIM()));
    status_label_->setStyleSheet(QString("color: %1; background: transparent; padding: 6px 8px; font-size:%2;")
                                     .arg(ui::colors::TEXT_SECONDARY(), kFontPx));
    scroll_area_->setStyleSheet(QStringLiteral("QScrollArea{background: transparent;} QScrollArea > QWidget > QWidget{background: transparent;}"));
    populate();
}

void OwnershipCalendarWidget::refresh_data() {
    set_loading(true);
    services::OwnershipService::instance().load_calendar(kDays);
}

void OwnershipCalendarWidget::populate() {
    const auto& c = services::OwnershipService::instance().calendar();
    // Clear the list, keeping the trailing stretch.
    while (list_layout_->count() > 1) {
        auto* item = list_layout_->takeAt(0);
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (!c.loaded) {
        status_label_->setText(QStringLiteral("Computing the next %1 days…").arg(kDays));
        return;
    }
    set_loading(false);
    if (!c.error.isEmpty()) {
        // Not BaseWidget::set_error — that clears the content layout and
        // deletes the body this widget keeps pointers into, and the next
        // refresh or theme change would then touch freed widgets. The
        // status line is the error surface here.
        status_label_->setText(QStringLiteral("Calendar unavailable: %1 — refresh to try again.").arg(c.error));
        return;
    }
    const QDate today = c.as_of.isValid() ? c.as_of : QDate::currentDate();
    int shown = 0;
    bool alt = false;
    for (const auto& e : c.entries) {
        if (!e.date.isValid())
            continue;
        auto* row = new QWidget;
        row->setStyleSheet(QString("background: %1;").arg(alt ? ui::colors::BG_RAISED() : QStringLiteral("transparent")));
        alt = !alt;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(8, 4, 8, 4);
        h->setSpacing(8);

        const int in_days = today.daysTo(e.date);
        auto* date = new QLabel(QStringLiteral("%1  <span style='color:%2;'>%3</span>")
                                    .arg(e.date.toString(QStringLiteral("d MMM")), ui::colors::TEXT_SECONDARY(),
                                         in_days == 0 ? QStringLiteral("today")
                                                      : QStringLiteral("in %1d").arg(in_days)));
        date->setTextFormat(Qt::RichText);
        date->setStyleSheet(QString("color: %1; background: transparent; font-size:%2;")
                                .arg(ui::colors::TEXT_PRIMARY(), kFontPx));
        h->addWidget(date, 2);

        auto* kind = new QLabel(kind_label(e.kind));
        kind->setStyleSheet(QString("color: %1; background: transparent; font-size:%2; font-weight:600;")
                                .arg(kind_colour(e.kind), kFontPx));
        h->addWidget(kind, 3);

        QString what, tip;
        switch (e.kind) {
            case CalendarEntry::Kind::Form13FDeadline:
                what = QStringLiteral("Q%1 %2 holdings must be filed — the holder tables can change")
                           .arg((e.basis.month() - 1) / 3 + 1).arg(e.basis.year());
                tip = QStringLiteral("13F is due 45 days after the quarter end (%1). Most filers use the "
                                     "whole window, so the register on every stock page moves on this day.")
                          .arg(e.basis.toString(QStringLiteral("d MMM yyyy")));
                break;
            case CalendarEntry::Kind::FinraPublication:
                what = QStringLiteral("FINRA publishes the %1 settlement").arg(e.basis.toString(QStringLiteral("d MMM")));
                tip = QStringLiteral("Firms report short positions two business days after settlement; "
                                     "FINRA publishes about eight business days after that. The short "
                                     "figures on the stock pages and the SHORT-CONSTRAINED scan refresh then.");
                break;
            case CalendarEntry::Kind::LockupExpiry:
                what = QStringLiteral("<b>%1</b> — %2 (priced %3)")
                           .arg(e.symbol, e.company, e.basis.toString(QStringLiteral("d MMM")));
                tip = QStringLiteral("The customary 180-day lock-up after an IPO — a convention, not a "
                                     "per-deal fact; the prospectus governs. When it ends, insider and "
                                     "pre-IPO shares can trade: the float can grow sharply. Click to open.");
                break;
        }
        auto* what_lbl = new QLabel(what);
        what_lbl->setTextFormat(Qt::RichText);
        what_lbl->setStyleSheet(QString("color: %1; background: transparent; font-size:%2;")
                                    .arg(ui::colors::TEXT_PRIMARY(), kFontPx));
        what_lbl->setWordWrap(true);
        h->addWidget(what_lbl, 5);
        row->setToolTip(ui::tooltip_wrap(tip));
        if (!e.symbol.isEmpty()) {
            row->setCursor(Qt::PointingHandCursor);
            const QString sym = e.symbol;
            row->installEventFilter(this);
            row->setProperty("symbol", sym);
        }
        list_layout_->insertWidget(list_layout_->count() - 1, row);
        ++shown;
    }
    status_label_->setText(QStringLiteral("%1 events in the next %2 days · SPACs excluded from lock-ups")
                               .arg(shown).arg(c.days));
}

} // namespace fincept::screens::widgets
