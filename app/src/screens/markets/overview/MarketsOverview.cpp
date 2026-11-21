#include "screens/markets/overview/MarketsOverview.h"

#include "screens/dashboard/canvas/AddWidgetDialog.h"
#include "screens/dashboard/canvas/DashboardCanvas.h"
#include "screens/dashboard/canvas/LayoutCodec.h"
#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/markets/MarketPanelStore.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollArea>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {

constexpr const char* kLayoutKey = "markets_overview_layout";

/// An old PANELS column name → the Quote List column key ("" = none: SYMBOL
/// and NAME are the row's name; PRESSURE has no column now).
QString column_key(const QString& old) {
    static const QHash<QString, QString> k = {{"LAST", "last"}, {"CHG", "chg"}, {"CHG%", "pct"}, {"HIGH", "high"},
                                              {"LOW", "low"},   {"VOL", "vol"}, {"BID", "bid"},  {"ASK", "ask"}};
    return k.value(old);
}

/// The old CONDITIONS page's saved list for `id` ({} when never edited).
QJsonObject conditions_cfg(const QString& id) {
    const auto r = SettingsRepository::instance().get(QStringLiteral("markets_conditions.cfg.") + id);
    return r.is_ok() ? QJsonDocument::fromJson(r.value().toUtf8()).object() : QJsonObject();
}

/// Left-to-right, top-to-bottom placement on the 12-column grid.
struct Packer {
    int x = 0, y = 0, row_h = 0;
    QVector<GridItem> items;
    void add(const QString& id, int w, int h, const QJsonObject& cfg = {}) {
        if (x + w > 12) {
            x = 0;
            y += row_h;
            row_h = 0;
        }
        GridItem it;
        it.id = id;
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        it.cell.x = x;
        it.cell.y = y;
        it.cell.w = w;
        it.cell.h = h;
        if (const auto* meta = WidgetRegistry::instance().find(id)) {
            it.cell.min_w = meta->min_w;
            it.cell.min_h = meta->min_h;
        }
        it.config = cfg;
        items.append(it);
        x += w;
        row_h = std::max(row_h, h);
    }
};

} // namespace

GridLayout MarketsOverview::default_layout() {
    Packer p;
    // The chart the lists drive, then the user's own panels as Quote Lists.
    p.add(QStringLiteral("market_chart"), 6, 8);
    for (const auto& cfg : MarketPanelStore::instance().load()) {
        QJsonArray rows;
        for (const auto& s : cfg.symbols)
            rows.append(QJsonObject{{"symbol", s}, {"label", QString()}});
        QStringList cols;
        for (const auto& c : cfg.column_order)
            if (const QString k = column_key(c); !k.isEmpty() && !cols.contains(k))
                cols << k;
        QJsonObject q{{"title", cfg.title}, {"rows", rows}};
        if (!cols.isEmpty())
            q["columns"] = QJsonArray::fromStringList(cols);
        p.add(QStringLiteral("quote_list"), 3, 4, q);
    }
    // The market-condition widgets, with any edits made on the old page.
    for (const QString id : {"cond_vol", "cond_curve", "cond_credit", "cond_macro", "cond_futures", "cond_fx",
                             "cond_commod", "cond_crypto", "cond_corr", "cond_odds"}) {
        const auto* meta = WidgetRegistry::instance().find(id);
        if (!meta)
            continue;
        p.add(id, meta->default_w, meta->default_h, conditions_cfg(id));
    }
    GridLayout layout;
    layout.cols = 12;
    layout.items = compact_vertical(p.items);
    return layout;
}

MarketsOverview::MarketsOverview(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    canvas_ = new DashboardCanvas;
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(false); // the canvas sizes itself
    scroll_->setMinimumWidth(0);
    scroll_->setWidget(canvas_);
    scroll_->setStyleSheet(QString("QScrollArea{border:none;background:%1;}"
                                   "QScrollBar:vertical{width:6px;background:transparent;}"
                                   "QScrollBar::handle:vertical{background:%2;border-radius:3px;min-height:20px;}"
                                   "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
                               .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
    scroll_->viewport()->installEventFilter(this);
    root->addWidget(scroll_, 1);

    save_timer_ = new QTimer(this);
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(800);
    connect(save_timer_, &QTimer::timeout, this, &MarketsOverview::save);
    connect(canvas_, &DashboardCanvas::layout_changed, this, [this](const GridLayout&) { save_timer_->start(); });
}

void MarketsOverview::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!restored_) {
        restored_ = true;
        restore();
    }
}

bool MarketsOverview::eventFilter(QObject* obj, QEvent* e) {
    if (obj == scroll_->viewport() && e->type() == QEvent::Resize) {
        const int vp_w = scroll_->viewport()->width();
        if (vp_w > 0 && vp_w != canvas_->width()) {
            canvas_->setMinimumWidth(0);
            canvas_->setMaximumWidth(QWIDGETSIZE_MAX);
            canvas_->resize(vp_w, canvas_->height());
        }
    }
    return QWidget::eventFilter(obj, e);
}

void MarketsOverview::save() {
    save_timer_->stop();
    SettingsRepository::instance().set(kLayoutKey, layout_codec::encode(canvas_->current_layout()), "markets");
}

void MarketsOverview::restore() {
    const auto r = SettingsRepository::instance().get(kLayoutKey);
    const auto layout = r.is_ok() ? layout_codec::decode(r.value()) : std::nullopt;
    if (layout) {
        canvas_->load_layout(*layout);
        return;
    }
    canvas_->load_layout(default_layout());
    save();
}

void MarketsOverview::add_widget_dialog() {
    auto* dlg = new AddWidgetDialog(this);
    connect(dlg, &AddWidgetDialog::widget_selected, canvas_, &DashboardCanvas::add_widget);
    dlg->exec();
    dlg->deleteLater();
}

void MarketsOverview::reset_layout() {
    canvas_->load_layout(default_layout());
    save();
}

void MarketsOverview::refresh_all() {
    for (auto* w : canvas_->findChildren<widgets::BaseWidget*>())
        emit w->refresh_requested();
}

} // namespace finterm::screens
