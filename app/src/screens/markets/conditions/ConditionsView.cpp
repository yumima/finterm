#include "screens/markets/conditions/ConditionsView.h"

#include "screens/dashboard/widgets/ConditionWidgets.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/RememberedSplitter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QSettings>
#include <QShowEvent>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {
constexpr const char* kHiddenKey = "markets_conditions.hidden";
constexpr const char* kCfgPrefix = "markets_conditions.cfg.";
constexpr const char* kCategory = "markets";
constexpr int kColumns = 3;
} // namespace

ConditionsView::ConditionsView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    add_btn_ = new QPushButton(QStringLiteral("+ WIDGET"));
    add_btn_->setFixedHeight(22);
    add_btn_->setCursor(Qt::PointingHandCursor);
    add_btn_->setToolTip(QStringLiteral("Bring back a condition widget hidden from this page"));
    connect(add_btn_, &QPushButton::clicked, this, &ConditionsView::show_add_menu);

    const auto r = SettingsRepository::instance().get(kHiddenKey);
    if (r.is_ok())
        for (const auto& v : QJsonDocument::fromJson(r.value().toUtf8()).array())
            hidden_ << v.toString();
    build();
}

QJsonObject ConditionsView::load_cfg(const QString& id) const {
    const auto r = SettingsRepository::instance().get(kCfgPrefix + id);
    if (!r.is_ok() || r.value().isEmpty())
        return {};
    return QJsonDocument::fromJson(r.value().toUtf8()).object();
}

void ConditionsView::save_cfg(const QString& id, const QJsonObject& cfg) {
    SettingsRepository::instance().set(kCfgPrefix + id,
                                       QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)), kCategory);
}

void ConditionsView::save_hidden() {
    SettingsRepository::instance().set(
        kHiddenKey, QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(hidden_)).toJson(QJsonDocument::Compact)),
        kCategory);
}

void ConditionsView::build() {
    if (cols_) {
        cols_->hide();
        cols_->deleteLater();
    }
    widgets_.clear();
    cols_ = new QSplitter(Qt::Horizontal, this);
    cols_->setHandleWidth(3);
    cols_->setChildrenCollapsible(false);
    QVector<QSplitter*> col(kColumns);
    for (int c = 0; c < kColumns; ++c) {
        col[c] = new QSplitter(Qt::Vertical);
        col[c]->setHandleWidth(3);
        col[c]->setChildrenCollapsible(false);
        cols_->addWidget(col[c]);
    }
    int i = 0;
    QStringList shown;
    for (const auto& d : widgets::condition_widget_defs()) {
        if (hidden_.contains(d.type_id))
            continue;
        const QString id = d.type_id;
        auto* w = widgets::create_condition_widget(id, load_cfg(id));
        if (!w)
            continue;
        connect(w, &widgets::BaseWidget::config_changed, this, [this, id](const QJsonObject& cfg) { save_cfg(id, cfg); });
        connect(w, &widgets::BaseWidget::close_requested, this, [this, id]() {
            if (!hidden_.contains(id))
                hidden_ << id;
            save_hidden();
            if (auto* wd = widgets_.take(id)) {
                wd->hide();
                wd->deleteLater();
            }
        });
        auto* split = col[i % kColumns];
        split->addWidget(w);
        // First arrangement (no saved sizes): each widget gets its natural
        // share of the column's height.
        split->setStretchFactor(split->count() - 1, d.h);
        widgets_.insert(id, w);
        shown << id;
        ++i;
    }
    for (int c = 0; c < kColumns; ++c)
        cols_->setStretchFactor(c, 1); // equal columns until the user drags them
    // Sizes are kept per arrangement: hiding a widget is a new arrangement.
    const QString key = QStringLiteral("markets_conditions.split.") + QString::number(qHash(shown.join(',')), 16);
    split_key_ = key;
    equalised_ = false;
    ui::remember_splitter(cols_, key + QStringLiteral(".cols"));
    for (int c = 0; c < kColumns; ++c)
        ui::remember_splitter(col[c], key + QStringLiteral(".c%1").arg(c));
    static_cast<QVBoxLayout*>(layout())->addWidget(cols_, 1);
    // Rebuilt while on screen (a widget brought back): size it now.
    QTimer::singleShot(0, this, [this]() { equalise(); });
}

void ConditionsView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // After the layout pass: the splitter's own width is not final here.
    QTimer::singleShot(0, this, [this]() { equalise(); });
}

void ConditionsView::equalise() {
    if (equalised_ || !cols_ || !isVisible())
        return;
    equalised_ = true;
    // No sizes saved for this arrangement: equal columns. (Stretch factors
    // lose to a wide table's size hint, which collapsed the third column.)
    if (QSettings().value(QStringLiteral("splitters/") + split_key_ + QStringLiteral(".cols")).isNull()) {
        const int w = std::max(300, static_cast<int>(cols_->width() / kColumns));
        cols_->setSizes(QList<int>(kColumns, w));
    }
}

void ConditionsView::show_add_menu() {
    QMenu menu(this);
    for (const auto& d : widgets::condition_widget_defs()) {
        if (!hidden_.contains(d.type_id))
            continue;
        auto* a = menu.addAction(d.name);
        a->setToolTip(d.description);
        const QString id = d.type_id;
        connect(a, &QAction::triggered, this, [this, id]() {
            hidden_.removeAll(id);
            save_hidden();
            build();
        });
    }
    if (menu.isEmpty()) {
        auto* a = menu.addAction(QStringLiteral("Every condition widget is on the page"));
        a->setEnabled(false);
    }
    menu.exec(add_btn_->mapToGlobal(QPoint(0, add_btn_->height())));
}

void ConditionsView::refresh_all() {
    for (auto* w : std::as_const(widgets_))
        emit w->refresh_requested();
}

} // namespace finterm::screens
