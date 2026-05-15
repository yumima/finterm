// src/screens/pre_ipo/views/PipelineView.cpp
#include "screens/pre_ipo/views/PipelineView.h"

#include "ui/theme/Theme.h"

#include <QDate>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

PipelineView::PipelineView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void PipelineView::build_ui() {
    setObjectName("preIpoPipeline");
    setStyleSheet(QString("#preIpoPipeline{background:%1;}").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    auto* row = new QHBoxLayout;
    auto* title = new QLabel("IPO PIPELINE");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:800;letter-spacing:1.5px;background:transparent;")
            .arg(colors::AMBER()));
    row->addWidget(title);
    auto* sub = new QLabel("S-1, S-1/A, F-1 filers from SEC EDGAR — amendment cadence signals pricing window");
    sub->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    row->addWidget(sub);
    row->addStretch();
    count_lbl_ = new QLabel("0 filers");
    count_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(colors::CYAN()));
    row->addWidget(count_lbl_);
    root->addLayout(row);

    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet(
        QString("QScrollArea{border:none;background:%1;}"
                "QScrollBar:vertical{width:6px;background:transparent;}"
                "QScrollBar::handle:vertical{background:%2;border-radius:3px;min-height:20px;}"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;}")
            .arg(colors::BG_BASE(), colors::BORDER_MED()));

    grid_host_ = new QWidget;
    grid_host_->setStyleSheet(QString("background:%1;").arg(colors::BG_BASE()));
    grid_ = new QGridLayout(grid_host_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(10);
    for (int c = 0; c < kColumns; ++c) grid_->setColumnStretch(c, 1);

    scroll_->setWidget(grid_host_);
    root->addWidget(scroll_, 1);
}

void PipelineView::set_pipeline(const QVector<S1Filing>& pipeline,
                                const QVector<PrivateCompany>& companies) {
    pipeline_  = pipeline;
    companies_ = companies;
    rebuild_grid();
}

void PipelineView::rebuild_grid() {
    // Clear existing cards
    while (auto* item = grid_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QHash<QString, QString> cik_to_id;
    for (const auto& c : companies_)
        if (!c.cik.isEmpty()) cik_to_id[c.cik] = c.id;

    // Loading state when nothing's arrived yet — single placeholder card.
    if (pipeline_.isEmpty()) {
        auto* placeholder = new QLabel(
            "Loading SEC S-1 / F-1 filings…\n\n"
            "First load can take ~60–90 seconds; subsequent visits load instantly from cache.");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setWordWrap(true);
        placeholder->setStyleSheet(
            QString("color:%1;font-size:13px;padding:40px;background:%2;"
                    "border:1px dashed %3;border-radius:4px;")
                .arg(colors::TEXT_SECONDARY(), colors::BG_SURFACE(), colors::BORDER_DIM()));
        grid_->addWidget(placeholder, 0, 0, 1, kColumns);
        count_lbl_->setText("loading…");
        return;
    }

    // Sort by estimated IPO date *closest-to-today first* (i.e. soonest
    // expected pricing). The actionable card for a pre-IPO investor is
    // "who's IPO-ing in the next 2 weeks", not "who filed S-1 most
    // recently". Estimated IPO = filed_date + 90d (sector-median lag);
    // companies past that 90d window count as imminent/overdue and
    // sort to the very top. Filings without a valid date fall to the
    // bottom.
    const QDate today = QDate::currentDate();

    // Priority: use actual_ipo_date (from Nasdaq) when available; fall back
    // to filed_date + 90-day sector-median estimate for EDGAR-only filers.
    auto days_to_price = [&](const S1Filing& s) -> qint64 {
        if (s.has_actual_date && s.actual_ipo_date.isValid())
            return today.daysTo(s.actual_ipo_date); // signed: negative = already pricing
        if (!s.filed_date.isValid())
            return std::numeric_limits<qint64>::max();
        // Estimated: filed + 90 days. Returning the signed value means overdue
        // items (est in the past) get negative values and sort to the very top —
        // the previous code returned -d for negative d, which inverted the order
        // and pushed the most-overdue items DOWN instead of up.
        return today.daysTo(s.filed_date.addDays(90));
    };

    QVector<S1Filing> sorted = pipeline_;
    std::sort(sorted.begin(), sorted.end(),
              [&](const S1Filing& a, const S1Filing& b) {
                  const qint64 da = days_to_price(a);
                  const qint64 db = days_to_price(b);
                  if (da != db) return da < db; // soonest (most negative) first
                  // Tiebreak: more amendments = further in pricing process
                  if (a.amendment_count != b.amendment_count)
                      return a.amendment_count > b.amendment_count;
                  return a.filed_date > b.filed_date;
              });

    // Column-major layout: fill column 0 top→bottom, then column 1, then column 2.
    // The user reads down each column, so the closest IPOs occupy the left column.
    const int total        = sorted.size();
    const int rows_per_col = (total + kColumns - 1) / kColumns;

    int idx = 0;
    for (const auto& s : sorted) {
        auto* card = make_card(s, cik_to_id.value(s.cik));
        const int col = idx / rows_per_col;
        const int row = idx % rows_per_col;
        grid_->addWidget(card, row, col);
        ++idx;
    }
    grid_->setRowStretch(rows_per_col + 1, 1);
    const QString src_note = sorted.isEmpty() ? "" :
        (sorted.first().has_actual_date ? " (incl. Nasdaq)" : " (EDGAR S-1/F-1)");
    count_lbl_->setText(QString::number(total) + " filers" + src_note);
}

QWidget* PipelineView::make_card(const S1Filing& s, const QString& company_id) const {
    auto* card = new QWidget;
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedHeight(124);
    card->setStyleSheet(
        QString("QWidget{background:%1;border:1px solid %2;border-radius:4px;}"
                "QWidget:hover{border-color:%3;}")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM(), colors::AMBER()));

    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(4);

    // Row 1: company name + amendment chip
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(6);
    auto* name = new QLabel(s.company_name);
    name->setStyleSheet(
        QString("color:%1;font-size:13px;font-weight:700;background:transparent;")
            .arg(colors::CYAN()));
    name->setWordWrap(true);
    row1->addWidget(name, 1);

    if (s.amendment_count > 0) {
        auto* amend = new QLabel("A×" + QString::number(s.amendment_count));
        const QString amend_color = s.amendment_count >= 3
            ? colors::AMBER() : colors::TEXT_SECONDARY();
        amend->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:700;"
                    "background:rgba(217,119,6,0.12);border:1px solid %1;"
                    "border-radius:3px;padding:1px 6px;")
                .arg(amend_color));
        row1->addWidget(amend);
    }
    vl->addLayout(row1);

    // Row 2: first-filed date
    const QDate today = QDate::currentDate();
    auto* date_lbl = new QLabel(
        s.filed_date.isValid()
            ? "Filed " + s.filed_date.toString("MMM d, yyyy")
            : "Filed —");
    date_lbl->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    vl->addWidget(date_lbl);

    // Row 3: days-since-filed + window estimate
    auto* row3 = new QHBoxLayout;
    row3->setSpacing(6);
    const int days = s.filed_date.isValid() ? static_cast<int>(s.filed_date.daysTo(today)) : -1;
    QString window = "—";
    if (days >= 0) {
        if (days < 60)       window = "<3 mo";
        else if (days < 120) window = "3–6 mo";
        else if (days < 365) window = "6–12 mo";
        else                 window = ">12 mo";
    }

    auto* dte = new QLabel(days >= 0 ? QString::number(days) + " days since file" : "—");
    dte->setStyleSheet(
        QString("color:%1;font-size:12px;font-family:Consolas,monospace;background:transparent;")
            .arg(colors::TEXT_PRIMARY()));
    row3->addWidget(dte, 1);

    auto* win_chip = new QLabel(window);
    win_chip->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:600;"
                "background:rgba(6,182,212,0.12);border:1px solid %1;"
                "border-radius:3px;padding:1px 6px;")
            .arg(colors::CYAN()));
    row3->addWidget(win_chip);
    vl->addLayout(row3);

    // Row 4: IPO date — confirmed (from Nasdaq) or estimated (filed + 90d).
    {
        QString date_text;
        QString date_color = colors::TEXT_SECONDARY();
        if (s.has_actual_date && s.actual_ipo_date.isValid()) {
            const int d = today.daysTo(s.actual_ipo_date);
            date_text  = d <= 0 ? "IPO: TODAY / PRICING"
                                : QString("IPO: %1").arg(s.actual_ipo_date.toString("MMM d, yyyy"));
            date_color = d <= 0 ? colors::POSITIVE() : colors::AMBER();
        } else if (s.filed_date.isValid() && days >= 0) {
            const int est_days = std::max(0, 90 - days);
            const QDate est_date = today.addDays(est_days);
            date_text  = est_days == 0
                ? QStringLiteral("Est. IPO: overdue (pricing imminent)")
                : QString("Est. IPO: ~%1").arg(est_date.toString("MMM d, yyyy"));
            date_color = est_days < 30 ? colors::AMBER() : colors::TEXT_SECONDARY();
        }
        if (!date_text.isEmpty()) {
            auto* est_lbl = new QLabel(date_text);
            est_lbl->setStyleSheet(
                QString("color:%1;font-size:12px;font-family:Consolas,monospace;"
                        "background:transparent;").arg(date_color));
            vl->addWidget(est_lbl);
        }
    }

    vl->addStretch();

    // Click overlay — same pattern as PicksView.
    auto* overlay = new QPushButton(card);
    overlay->setFlat(true);
    overlay->setCursor(Qt::PointingHandCursor);
    overlay->setStyleSheet("QPushButton{background:transparent;border:none;}");
    overlay->setGeometry(card->rect());
    overlay->raise();
    overlay->show();
    card->setProperty("_overlay", QVariant::fromValue(static_cast<QObject*>(overlay)));
    card->installEventFilter(const_cast<PipelineView*>(this));

    const QString id = company_id;
    auto* self = const_cast<PipelineView*>(this);
    QObject::connect(overlay, &QPushButton::clicked, self, [self, id] {
        if (!id.isEmpty()) emit self->company_selected(id);
    });

    return card;
}

bool PipelineView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            auto* obj = w->property("_overlay").value<QObject*>();
            if (auto* btn = qobject_cast<QPushButton*>(obj))
                btn->setGeometry(w->rect());
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace fincept::screens
