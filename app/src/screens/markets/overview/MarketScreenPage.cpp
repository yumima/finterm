#include "screens/markets/overview/MarketScreenPage.h"

#include "screens/dashboard/canvas/AddWidgetDialog.h"
#include "screens/dashboard/canvas/DashboardCanvas.h"
#include "screens/dashboard/canvas/LayoutCodec.h"
#include "screens/dashboard/canvas/SelectionLink.h"
#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/markets/overview/MarketTemplates.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens {

MarketScreenPage::MarketScreenPage(const QString& id, const QString& template_id, QWidget* parent)
    : QWidget(parent), id_(id), template_(template_id) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    canvas_ = new DashboardCanvas;
    canvas_->set_fit_exact(true);
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
    connect(save_timer_, &QTimer::timeout, this, &MarketScreenPage::save);
    connect(canvas_, &DashboardCanvas::layout_changed, this, [this](const GridLayout&) {
        save_timer_->start();
        // A tile added, removed or resized changes the rows: refit the view
        // (a second pass finds the same height and stops).
        canvas_->fit_rows_to_viewport(scroll_->viewport()->height());
    });
}

QString MarketScreenPage::key() const {
    return QStringLiteral("markets_screen:") + id_;
}

void MarketScreenPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!restored_) { // built on first show: a screen never opened costs nothing
        restored_ = true;
        restore();
    }
}

bool MarketScreenPage::eventFilter(QObject* obj, QEvent* e) {
    if (obj == scroll_->viewport() && e->type() == QEvent::Resize) {
        const int vp_w = scroll_->viewport()->width();
        const int vp_h = scroll_->viewport()->height();
        if (vp_w > 0 && vp_w != canvas_->width()) {
            canvas_->setMinimumWidth(0);
            canvas_->setMaximumWidth(QWIDGETSIZE_MAX);
            canvas_->resize(vp_w, canvas_->height());
        }
        // A screen is read at a glance: its rows stretch to fill the view.
        if (vp_h > 0)
            canvas_->fit_rows_to_viewport(vp_h);
    }
    return QWidget::eventFilter(obj, e);
}

void MarketScreenPage::save() {
    save_timer_->stop();
    if (!restored_)
        return; // never shown: nothing of the user's to save
    SettingsRepository::instance().set(key(), layout_codec::encode(canvas_->current_layout()), "markets");
}

void MarketScreenPage::restore() {
    const auto r = SettingsRepository::instance().get(key());
    const auto layout = r.is_ok() ? layout_codec::decode(r.value()) : std::nullopt;
    canvas_->load_layout(layout ? *layout : markets::template_layout(template_));
    fit_to_view();
    if (!layout)
        save();
}

void MarketScreenPage::fit_to_view() {
    const int vp_w = scroll_->viewport()->width();
    if (vp_w > 0 && vp_w != canvas_->width())
        canvas_->resize(vp_w, canvas_->height());
    if (const int vp_h = scroll_->viewport()->height(); vp_h > 0)
        canvas_->fit_rows_to_viewport(vp_h);
}

void MarketScreenPage::load_layout(const GridLayout& layout) {
    restored_ = true;
    canvas_->load_layout(layout);
    fit_to_view();
    save();
}

GridLayout MarketScreenPage::layout() const {
    if (!restored_) {
        const auto r = SettingsRepository::instance().get(key());
        const auto saved = r.is_ok() ? layout_codec::decode(r.value()) : std::nullopt;
        return saved ? *saved : markets::template_layout(template_);
    }
    return canvas_->current_layout();
}

void MarketScreenPage::forget() {
    save_timer_->stop();
    SettingsRepository::instance().remove(key());
}

void MarketScreenPage::add_widget_dialog() {
    auto* dlg = new AddWidgetDialog(this);
    connect(dlg, &AddWidgetDialog::widget_selected, canvas_, &DashboardCanvas::add_widget);
    dlg->exec();
    dlg->deleteLater();
}

void MarketScreenPage::reset_layout() {
    restored_ = true;
    canvas_->load_layout(markets::template_layout(template_));
    fit_to_view();
    save();
}

void MarketScreenPage::refresh_all() {
    for (auto* w : canvas_->findChildren<widgets::BaseWidget*>())
        emit w->refresh_requested();
}

void MarketScreenPage::select(const QString& symbol, const QString& label) {
    if (!restored_) {
        restored_ = true;
        restore();
    }
    if (auto* link = SelectionLink::of(canvas_))
        link->select(symbol, label);
}

} // namespace finterm::screens
