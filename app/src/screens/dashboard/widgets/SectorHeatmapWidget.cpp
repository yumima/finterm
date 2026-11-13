#include "screens/dashboard/widgets/SectorHeatmapWidget.h"

#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QLabel>

#include <cmath>

namespace finterm::screens::widgets {

QStringList SectorHeatmapWidget::sector_symbols() {
    return {"XLK", "XLV", "XLF", "XLE", "XLY", "XLI", "XLB", "XLU", "XLRE", "XLC", "XLP", "SOXX"};
}

QMap<QString, QString> SectorHeatmapWidget::sector_labels() {
    return {
        {"XLK", "Technology"},     {"XLV", "Healthcare"},  {"XLF", "Financials"},     {"XLE", "Energy"},
        {"XLY", "Consumer Disc."}, {"XLI", "Industrials"}, {"XLB", "Materials"},      {"XLU", "Utilities"},
        {"XLRE", "Real Estate"},   {"XLC", "Comm. Svc."},  {"XLP", "Consumer Stap."}, {"SOXX", "Semis"},
    };
}

SectorHeatmapWidget::SectorHeatmapWidget(QWidget* parent) : BaseWidget("SECTOR HEATMAP", parent) {
    build_grid();

    connect(this, &BaseWidget::refresh_requested, this, &SectorHeatmapWidget::refresh_data);

    apply_styles();
    set_loading(true);
}

void SectorHeatmapWidget::build_grid() {
    grid_container_ = new QWidget(this);
    grid_ = new QGridLayout(grid_container_);
    grid_->setContentsMargins(4, 4, 4, 4);
    grid_->setSpacing(3);

    content_layout()->addWidget(grid_container_);
}

void SectorHeatmapWidget::apply_styles() {
    // The grid cells are rebuilt dynamically in populate() using current tokens.
    // Re-populate with existing data to pick up new theme colors.
    // If no data yet, nothing to restyle.
}

void SectorHeatmapWidget::on_theme_changed() {
    apply_styles();
    // Re-populate the grid with current data so cells pick up new token values
    cell_syms_.clear(); // persistent cells carry the old theme's styles: rebuild
    rebuild_from_cache(); // now — an unchanged re-poll is not re-delivered by the hub
    if (grid_->count() > 0)
        refresh_data();
}

void SectorHeatmapWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!hub_active_)
        hub_subscribe_all();
}

void SectorHeatmapWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (hub_active_)
        hub_unsubscribe_all();
}

void SectorHeatmapWidget::refresh_data() {
    auto& hub = datahub::DataHub::instance();
    QStringList topics;
    for (const auto& sym : sector_symbols())
        topics.append(QStringLiteral("market:quote:") + sym);
    hub.request(topics, /*force=*/true); // user-triggered: bypass min_interval
}

void SectorHeatmapWidget::hub_subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    for (const auto& sym : sector_symbols()) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            row_cache_.insert(sym, v.value<services::QuoteData>());
            set_loading(false);
            clear_error();
            quote_render_.request(); // one render per tick batch
        });
    }
    // Surface a fetch failure of its own quotes instead of an endless spinner. Error
    // only while nothing has loaded — a single ETF's transient error must not
    // wipe a populated heatmap.
    // Errors for THIS widget's own topics only (a per-topic subscription —
    // a market:quote:* pattern also delivered every other widget's bad ticker).
    for (const auto& sym : sector_symbols()) {
        hub.subscribe_errors(this, QStringLiteral("market:quote:") + sym, [this](const QString&) {
            set_loading(false);
            if (row_cache_.isEmpty())
                set_error(QStringLiteral("Sector data unavailable"));
        });
    }
    hub_active_ = true;
}

void SectorHeatmapWidget::hub_unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    for (const auto& sym : sector_symbols())
        hub.unsubscribe_errors(this, QStringLiteral("market:quote:") + sym);
    hub_active_ = false;
}

void SectorHeatmapWidget::rebuild_from_cache() {
    QVector<services::QuoteData> quotes;
    quotes.reserve(row_cache_.size());
    for (const auto& sym : sector_symbols()) {
        auto it = row_cache_.constFind(sym);
        if (it != row_cache_.constEnd())
            quotes.append(it.value());
    }
    if (!quotes.isEmpty())
        populate(quotes);
}

namespace {
// One styling rule for both paths of populate() (fresh cells and live retints).
QString heatmap_cell_bg(double change_pct) {
    // Unknown change (NaN, no previous close) → neutral, untinted cell.
    const bool known = std::isfinite(change_pct);
    const int intensity = known ? static_cast<int>(std::min(std::abs(change_pct) * 60.0, 200.0)) : 0;
    QColor tint(!known              ? ui::colors::TEXT_TERTIARY()
                : change_pct >= 0 ? ui::colors::POSITIVE()
                                  : ui::colors::NEGATIVE());
    tint.setAlpha(40 + intensity);
    return tint.name(QColor::HexArgb);
}
QString heatmap_frame_style(const QString& bg) {
    return QString("background: %1; border: 1px solid %2; border-radius: 2px;").arg(bg, ui::colors::BORDER_DIM());
}
QString heatmap_label_style(const QString& color) {
    return QString("color: %1; font-weight: bold; background: transparent;").arg(color);
}
} // namespace

void SectorHeatmapWidget::populate(const QVector<services::QuoteData>& quotes) {
    QStringList syms;
    for (const auto& q : quotes)
        syms << q.symbol;
    if (syms == cell_syms_) {
        // Live tick: same cells — retint/relabel in place, restyling a cell only
        // when its tint actually changes (setStyleSheet is the expensive part).
        for (const auto& q : quotes) {
            Cell& c = cells_[q.symbol];
            const QString bg = heatmap_cell_bg(q.change_pct);
            if (bg != c.bg) {
                c.bg = bg;
                c.frame->setStyleSheet(heatmap_frame_style(bg));
            }
            c.chg->setText(ui::formatting::format_percent(q.change_pct, 2, true));
            const QString cc = ui::change_color(q.change_pct);
            if (cc != c.chg_color) {
                c.chg_color = cc;
                c.chg->setStyleSheet(heatmap_label_style(cc));
            }
        }
        return;
    }
    cell_syms_ = syms;
    cells_.clear();

    // Clear grid
    QLayoutItem* item;
    while ((item = grid_->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    auto labels = sector_labels();
    int idx = 0;
    for (const auto& q : quotes) {
        auto* cell = new QFrame;
        cell->setMinimumSize(80, 40);

        const QString bg_color = heatmap_cell_bg(q.change_pct);
        cell->setStyleSheet(heatmap_frame_style(bg_color));

        auto* cl = new QVBoxLayout(cell);
        cl->setContentsMargins(4, 2, 4, 2);
        cl->setSpacing(0);

        auto* name = new QLabel(labels.value(q.symbol, q.symbol));
        name->setStyleSheet(heatmap_label_style(ui::colors::TEXT_PRIMARY()));
        name->setAlignment(Qt::AlignCenter);
        cl->addWidget(name);

        auto* chg = new QLabel(ui::formatting::format_percent(q.change_pct, 2, true));
        const QString cc = ui::change_color(q.change_pct);
        chg->setStyleSheet(heatmap_label_style(cc));
        cells_.insert(q.symbol, Cell{cell, chg, bg_color, cc});
        chg->setAlignment(Qt::AlignCenter);
        cl->addWidget(chg);

        grid_->addWidget(cell, idx / 3, idx % 3);
        ++idx;
    }
}

} // namespace finterm::screens::widgets
