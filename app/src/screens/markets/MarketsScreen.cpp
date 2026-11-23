#include "screens/markets/MarketsScreen.h"

#include "core/events/EventBus.h"
#include "screens/dashboard/canvas/LayoutCodec.h"
#include "screens/markets/MarketPanelStore.h"
#include "screens/markets/overview/MarketScreenPage.h"
#include "screens/markets/overview/MarketTemplates.h"
#include "services/futures/FuturesCatalog.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimeZone>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {
constexpr const char* kScreensKey = "markets_screens";        ///< OVERVIEW screens: [{id, title, template}]
constexpr const char* kCurrentKey = "markets_screen_current"; ///< the page last shown (id)

QString lbl_ss(const QString& color, bool bold = false, int px = -1) {
    const int sz = (px > 0) ? px : ui::fonts::font_px(-2);
    return QString("color:%1;background:transparent;font-size:%2px;font-family:'%3';%4")
        .arg(color)
        .arg(sz)
        .arg(ui::fonts::DATA_FAMILY())
        .arg(bold ? "font-weight:bold;" : "");
}

QString futures_page_id(const QString& group) {
    return QStringLiteral("futures:") + group;
}
} // namespace

QPointer<MarketsScreen> MarketsScreen::live_;
bool MarketsScreen::pending_request_ = false;
QString MarketsScreen::pending_root_;

MarketsScreen::MarketsScreen(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    stack_ = new QStackedWidget(this);
    load_pages();
    header_bar_ = build_header_bar();
    root->addWidget(header_bar_);
    root->addWidget(stack_, 1);

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();

    const auto cur = SettingsRepository::instance().get(kCurrentKey);
    const int idx = cur.is_ok() ? index_of(cur.value()) : -1;
    show_page(idx >= 0 ? idx : 0, false);

    live_ = this;
    if (pending_request_) { // asked for before this screen existed
        pending_request_ = false;
        open_futures(pending_root_);
        pending_root_.clear();
        // The router restores the saved page right after construction; the
        // market asked for must win over it.
        answered_request_ = true;
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void MarketsScreen::load_pages() {
    auto& settings = SettingsRepository::instance();
    const auto r = settings.get(kScreensKey);
    const QJsonArray arr = r.is_ok() ? QJsonDocument::fromJson(r.value().toUtf8()).array() : QJsonArray();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        PageDef d{o.value("id").toString(), o.value("title").toString(), o.value("template").toString(),
                  PageDef::Overview};
        // Only OVERVIEW screens live in this list; the fixed FUTURES / WORLD
        // pages are known by their ids (a screen the user built from the
        // WORLD template is an OVERVIEW screen like any other).
        if (!d.id.isEmpty() && !d.title.isEmpty() && !d.id.startsWith(QLatin1String("futures:")) &&
            d.id != QLatin1String("world"))
            pages_.append(d);
    }
    if (pages_.isEmpty()) {
        // First open: the default screens — and the user's own PANELS when
        // they had made some, so nothing they built is lost.
        for (const auto& t : markets::default_screens())
            pages_.append({t.id, t.title, t.id, PageDef::Overview});
        if (MarketPanelStore::instance().has_saved())
            pages_.append({QStringLiteral("panels"), QStringLiteral("MY PANELS"), QStringLiteral("panels"),
                           PageDef::Overview});
        // The single OVERVIEW canvas of the build before screens: kept as a
        // screen of its own, arranged as the user left it.
        const auto old = settings.get(QStringLiteral("markets_overview_layout"));
        if (old.is_ok() && layout_codec::decode(old.value())) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            settings.set(QStringLiteral("markets_screen:") + id, old.value(), "markets");
            pages_.append({id, QStringLiteral("MY OVERVIEW"), QStringLiteral("blank"), PageDef::Overview});
        }
        save_overview();
    }
    for (const auto& g : services::futures::groups())
        pages_.append({futures_page_id(g), g, futures_page_id(g), PageDef::Futures});
    pages_.append({QStringLiteral("world"), QStringLiteral("WORLD"), QStringLiteral("world"), PageDef::World});
    widgets_.fill(nullptr, pages_.size());
}

void MarketsScreen::save_overview() {
    QJsonArray arr;
    for (const auto& d : pages_)
        if (d.kind == PageDef::Overview)
            arr.append(QJsonObject{{"id", d.id}, {"title", d.title}, {"template", d.tmpl}});
    SettingsRepository::instance().set(kScreensKey, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)),
                                       "markets");
}

MarketScreenPage* MarketsScreen::ensure_page(int index) {
    if (!widgets_[index]) {
        // Built on first show: a page never opened costs nothing.
        widgets_[index] = new MarketScreenPage(pages_[index].id, pages_[index].tmpl, stack_);
        stack_->addWidget(widgets_[index]);
    }
    return widgets_[index];
}

int MarketsScreen::index_of(const QString& id) const {
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].id == id)
            return i;
    return -1;
}

void MarketsScreen::show_page(int index, bool persist) {
    if (index < 0 || index >= pages_.size())
        return;
    current_ = index;
    stack_->setCurrentWidget(ensure_page(index));
    for (int i = 0; i < buttons_.size(); ++i)
        if (buttons_[i])
            buttons_[i]->setChecked(i == index);
    if (persist)
        SettingsRepository::instance().set(kCurrentKey, pages_[index].id, "markets");
}

void MarketsScreen::open_futures(const QString& root) {
    const QString group = root.isEmpty() ? services::futures::majors_group() : markets::futures_group_for(root);
    const int i = index_of(futures_page_id(group));
    if (i < 0)
        return;
    show_page(i);
    if (const auto* c = services::futures::find_contract(root))
        ensure_page(i)->select(services::futures::continuous_symbol(c->root),
                               c->root + QStringLiteral(" · ") + c->name);
}

void MarketsScreen::prepare_futures(const QString& root) {
    if (live_) {
        live_->open_futures(root);
    } else {
        pending_request_ = true;
        pending_root_ = root;
    }
}

void MarketsScreen::request_futures(const QString& root) {
    prepare_futures(root);
    EventBus::instance().publish("nav.switch_screen", QVariantMap{{"screen_id", "markets"}});
}

// ── OVERVIEW screen editing ─────────────────────────────────────────────────

void MarketsScreen::add_overview(const QString& title, const QString& tmpl, const GridLayout* layout) {
    int at = 0;
    while (at < pages_.size() && pages_[at].kind == PageDef::Overview)
        ++at;
    pages_.insert(at, {QUuid::createUuid().toString(QUuid::WithoutBraces), title, tmpl, PageDef::Overview});
    widgets_.insert(at, nullptr);
    buttons_.insert(at, nullptr);
    if (layout)
        ensure_page(at)->load_layout(*layout);
    save_overview();
    rebuild_overview_buttons();
    show_page(at);
}

void MarketsScreen::show_add_menu() {
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("Blank screen")), &QAction::triggered, this, [this]() {
        int n = 0;
        for (const auto& d : pages_)
            n += d.kind == PageDef::Overview;
        add_overview(QStringLiteral("SCREEN %1").arg(n + 1), QStringLiteral("blank"));
    });
    auto* from = menu.addMenu(QStringLiteral("From a template"));
    for (const auto& list : {markets::default_screens(), markets::extra_templates()})
        for (const auto& t : list) {
            auto* a = from->addAction(t.title);
            a->setToolTip(t.tip);
            connect(a, &QAction::triggered, this, [this, t]() { add_overview(t.title, t.id); });
        }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Reset all of MARKETS to default…")), &QAction::triggered, this,
            &MarketsScreen::reset_all);
    menu.exec(add_btn_->mapToGlobal(QPoint(0, add_btn_->height())));
}

void MarketsScreen::show_overview_menu(int index, const QPoint& global) {
    if (index < 0 || index >= pages_.size() || pages_[index].kind != PageDef::Overview)
        return;
    int count = 0;
    for (const auto& d : pages_)
        count += d.kind == PageDef::Overview;
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("Rename…")), &QAction::triggered, this, [this, index]() { rename_screen(index); });
    connect(menu.addAction(QStringLiteral("Duplicate")), &QAction::triggered, this,
            [this, index]() { duplicate_screen(index); });
    auto* left = menu.addAction(QStringLiteral("Move left"));
    left->setEnabled(index > 0);
    connect(left, &QAction::triggered, this, [this, index]() { move_screen(index, -1); });
    auto* right = menu.addAction(QStringLiteral("Move right"));
    right->setEnabled(index + 1 < count);
    connect(right, &QAction::triggered, this, [this, index]() { move_screen(index, +1); });
    menu.addSeparator();
    auto* del = menu.addAction(QStringLiteral("Delete"));
    del->setEnabled(count > 1);
    connect(del, &QAction::triggered, this, [this, index]() { delete_screen(index); });
    menu.exec(global);
}

void MarketsScreen::rename_screen(int index) {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Rename screen"), QStringLiteral("Name"),
                                            QLineEdit::Normal, pages_[index].title, &ok)
                          .trimmed()
                          .toUpper();
    if (!ok || t.isEmpty())
        return;
    pages_[index].title = t;
    buttons_[index]->setText(QString(t).replace(QLatin1Char('&'), QStringLiteral("&&")));
    save_overview();
}

void MarketsScreen::duplicate_screen(int index) {
    GridLayout layout = ensure_page(index)->layout();
    for (auto& it : layout.items)
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    add_overview(pages_[index].title + QStringLiteral(" 2"), pages_[index].tmpl, &layout);
}

void MarketsScreen::move_screen(int index, int delta) {
    const int j = index + delta;
    if (j < 0 || j >= pages_.size() || pages_[j].kind != PageDef::Overview)
        return;
    pages_.swapItemsAt(index, j);
    widgets_.swapItemsAt(index, j);
    buttons_.swapItemsAt(index, j);
    if (current_ == index)
        current_ = j;
    else if (current_ == j)
        current_ = index;
    save_overview();
    rebuild_overview_buttons();
}

void MarketsScreen::delete_screen(int index) {
    if (QMessageBox::question(this, QStringLiteral("Delete screen"),
                              QStringLiteral("Delete the screen “%1” and its widgets?").arg(pages_[index].title)) !=
        QMessageBox::Yes)
        return;
    auto* page = ensure_page(index);
    page->forget();
    stack_->removeWidget(page);
    page->deleteLater();
    pages_.removeAt(index);
    widgets_.removeAt(index);
    buttons_.removeAt(index);
    save_overview();
    rebuild_overview_buttons();
    show_page(std::max(0, index - 1));
}

void MarketsScreen::reset_all() {
    if (QMessageBox::question(this, QStringLiteral("Reset MARKETS"),
                              QStringLiteral("Put every MARKETS page back to its default: the OVERVIEW screens (screens "
                                             "you added are removed), each FUTURES page and WORLD?")) !=
        QMessageBox::Yes)
        return;
    for (int i = 0; i < pages_.size(); ++i) {
        if (widgets_[i]) {
            widgets_[i]->forget();
            stack_->removeWidget(widgets_[i]);
            widgets_[i]->deleteLater();
        } else {
            MarketScreenPage(pages_[i].id, pages_[i].tmpl).forget();
        }
    }
    pages_.clear();
    widgets_.clear();
    buttons_.clear();
    SettingsRepository::instance().remove(kScreensKey);
    load_pages();
    buttons_.fill(nullptr, pages_.size());
    rebuild_overview_buttons();
    // FUTURES and WORLD buttons keep their places: re-point them.
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].kind != PageDef::Overview)
            for (auto* b : header_bar_->findChildren<QPushButton*>())
                if (b->property("page").toString() == pages_[i].id)
                    buttons_[i] = b;
    show_page(0);
}

// ---------------------------------------------------------------------------
// State (the dock router keeps it per screen)
// ---------------------------------------------------------------------------

void MarketsScreen::restore_state(const QVariantMap& state) {
    if (answered_request_) { // a market was asked for (CURVE ▸): it stays
        answered_request_ = false;
        return;
    }
    const int i = index_of(state.value(QStringLiteral("page")).toString());
    if (i >= 0)
        show_page(i, false);
}

QVariantMap MarketsScreen::save_state() const {
    return {{QStringLiteral("page"), current_ >= 0 && current_ < pages_.size() ? pages_[current_].id : QString()}};
}

// ---------------------------------------------------------------------------
// Header: one row of pages
// ---------------------------------------------------------------------------

QPushButton* MarketsScreen::make_page_button(int index) {
    // "&" is a mnemonic marker in a button's text: "RATES & MACRO" needs "&&".
    auto* b = new QPushButton(QString(pages_[index].title).replace(QLatin1Char('&'), QStringLiteral("&&")));
    b->setObjectName(QStringLiteral("mkPage"));
    b->setCheckable(true);
    b->setFixedHeight(22);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("page", pages_[index].id);
    for (const auto& list : {markets::default_screens(), markets::extra_templates()})
        for (const auto& t : list)
            if (t.id == pages_[index].tmpl)
                b->setToolTip(t.tip);
    if (pages_[index].kind == PageDef::Futures)
        b->setToolTip(QStringLiteral("%1 futures: board, chart, forward curve, contract months, spread, positioning, "
                                     "ticket — click a market and the page follows")
                          .arg(pages_[index].title));
    const QString id = pages_[index].id;
    connect(b, &QPushButton::clicked, this, [this, id]() { show_page(index_of(id)); });
    if (pages_[index].kind == PageDef::Overview) {
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this,
                [this, id, b](const QPoint& pos) { show_overview_menu(index_of(id), b->mapToGlobal(pos)); });
    }
    return b;
}

void MarketsScreen::rebuild_overview_buttons() {
    while (auto* item = overview_layout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < pages_.size() && pages_[i].kind == PageDef::Overview; ++i) {
        buttons_[i] = make_page_button(i);
        overview_layout_->addWidget(buttons_[i]);
    }
    if (current_ >= 0 && current_ < buttons_.size() && buttons_[current_])
        buttons_[current_]->setChecked(true);
    refresh_theme();
}

QWidget* MarketsScreen::build_header_bar() {
    auto* w = new QWidget(this);
    w->setFixedHeight(36);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(2);
    auto label = [&](const QString& t, bool bold = true) {
        auto* l = new QLabel(t);
        l->setObjectName(QStringLiteral("mkGroup"));
        l->setStyleSheet(lbl_ss(bold ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_TERTIARY(), bold));
        return l;
    };
    h->addWidget(label(QStringLiteral("FINTERM MARKETS")));
    h->addSpacing(16);

    buttons_.fill(nullptr, pages_.size());
    h->addWidget(label(QStringLiteral("OVERVIEW:"), false));
    overview_box_ = new QWidget;
    overview_layout_ = new QHBoxLayout(overview_box_);
    overview_layout_->setContentsMargins(0, 0, 0, 0);
    overview_layout_->setSpacing(2);
    h->addWidget(overview_box_);
    add_btn_ = new QToolButton;
    add_btn_->setText(QStringLiteral("+"));
    add_btn_->setToolTip(QStringLiteral("Add an OVERVIEW screen — blank or from a template — or reset all of MARKETS. "
                                        "Right-click a screen to rename, duplicate, move or delete it."));
    add_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_btn_, &QToolButton::clicked, this, &MarketsScreen::show_add_menu);
    h->addWidget(add_btn_);
    h->addSpacing(18);

    h->addWidget(label(QStringLiteral("FUTURES:"), false));
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].kind == PageDef::Futures) {
            buttons_[i] = make_page_button(i);
            h->addWidget(buttons_[i]);
        }
    h->addSpacing(18);
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].kind == PageDef::World) {
            buttons_[i] = make_page_button(i);
            h->addWidget(buttons_[i]);
        }
    for (int i = 0; i < pages_.size() && pages_[i].kind == PageDef::Overview; ++i) {
        buttons_[i] = make_page_button(i);
        overview_layout_->addWidget(buttons_[i]);
    }

    h->addStretch(1);
    session_label_ = new QLabel;
    h->addWidget(session_label_);
    update_session_status();
    session_timer_ = new QTimer(this);
    session_timer_->setInterval(60000);
    connect(session_timer_, &QTimer::timeout, this, &MarketsScreen::update_session_status);
    h->addSpacing(10);

    auto* refresh_btn = new QPushButton("REFRESH");
    refresh_btn->setFixedHeight(20);
    refresh_btn->setObjectName("dtBtn");
    refresh_btn->setCursor(Qt::PointingHandCursor);
    refresh_btn->setToolTip("Ask every widget on this page for a fresh answer now (they refresh on their own too)");
    connect(refresh_btn, &QPushButton::clicked, this, &MarketsScreen::refresh_all);
    h->addWidget(refresh_btn);
    h->addSpacing(4);
    // The dashboard's own controls: + ADD a widget (the same picker), a
    // tile's ✕ removes it, RESET puts this page back.
    add_widget_btn_ = new QPushButton("+ ADD");
    add_widget_btn_->setFixedHeight(20);
    add_widget_btn_->setObjectName("dtAddBtn");
    add_widget_btn_->setCursor(Qt::PointingHandCursor);
    add_widget_btn_->setToolTip("Add a widget to this page — the dashboard's picker. A tile's ✕ removes it; the same "
                                "widget may sit on several pages (one fetch serves them all).");
    connect(add_widget_btn_, &QPushButton::clicked, this, [this]() {
        if (current_ >= 0)
            ensure_page(current_)->add_widget_dialog();
    });
    h->addWidget(add_widget_btn_);
    h->addSpacing(4);
    reset_btn_ = new QPushButton("RESET");
    reset_btn_->setFixedHeight(20);
    reset_btn_->setObjectName("dtResetBtn");
    reset_btn_->setCursor(Qt::PointingHandCursor);
    reset_btn_->setToolTip("Put this page back to its default widgets (all of MARKETS: the + menu)");
    connect(reset_btn_, &QPushButton::clicked, this, [this]() {
        if (current_ < 0)
            return;
        if (QMessageBox::question(this, QStringLiteral("Reset page"),
                                  QStringLiteral("Reset “%1” to its default widgets?").arg(pages_[current_].title)) ==
            QMessageBox::Yes)
            ensure_page(current_)->reset_layout();
    });
    h->addWidget(reset_btn_);
    return w;
}

// ---------------------------------------------------------------------------
// Session status — DST-correct via QTimeZone
// ---------------------------------------------------------------------------

void MarketsScreen::update_session_status() {
    QDateTime utc = QDateTime::currentDateTimeUtc();
    QTimeZone ny_tz("America/New_York");
    QDateTime et = utc.toTimeZone(ny_tz);
    int day = et.date().dayOfWeek();
    int hhmm = et.time().hour() * 100 + et.time().minute();
    bool weekday = (day >= 1 && day <= 5);

    QString label, color;
    if (!weekday || hhmm < 400 || hhmm >= 2000) {
        label = "NYSE: CLOSED";
        color = ui::colors::TEXT_SECONDARY();
    } else if (hhmm < 930) {
        label = "NYSE: PRE-MKT";
        color = ui::colors::AMBER();
    } else if (hhmm < 1600) {
        label = "NYSE: OPEN";
        color = ui::colors::POSITIVE();
    } else {
        label = "NYSE: AFTER-HRS";
        color = ui::colors::AMBER();
    }

    if (session_label_) {
        session_label_->setText(label);
        session_label_->setStyleSheet(lbl_ss(color, true, 11));
    }
}

void MarketsScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    update_session_status();
    session_timer_->start();
}

void MarketsScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    session_timer_->stop();
}

void MarketsScreen::refresh_all() {
    if (current_ >= 0)
        ensure_page(current_)->refresh_all();
}

void MarketsScreen::refresh_theme() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    if (!header_bar_)
        return;
    using namespace ui::colors;
    header_bar_->setStyleSheet(
        QString("QWidget{background:%1;}"
                "QPushButton#mkPage{background:transparent;color:%2;border:1px solid %3;font-size:%4px;"
                "font-family:'%5';font-weight:700;padding:0 8px;}"
                "QPushButton#mkPage:checked{background:%6;color:%7;border-color:%6;}"
                "QPushButton#mkPage:hover:!checked{color:%8;}"
                "QToolButton{background:transparent;color:%6;border:1px solid %6;font-weight:700;padding:0 8px;}"
                "#dtBtn{background:%9;border:1px solid %3;color:%2;padding:0 10px;font-weight:bold;}"
                "#dtBtn:hover{background:%10;color:%8;border-color:%11;}"
                "#dtAddBtn{background:%9;border:1px solid %12;color:%6;padding:0 10px;font-weight:bold;}"
                "#dtAddBtn:hover{background:%10;color:%8;border-color:%11;}"
                "#dtResetBtn{background:%9;border:1px solid %13;color:%13;padding:0 10px;font-weight:bold;}"
                "#dtResetBtn:hover{background:%10;color:%8;border-color:%11;}")
            .arg(BG_RAISED(), TEXT_SECONDARY(), BORDER_MED())
            .arg(ui::fonts::font_px(-3))
            .arg(ui::fonts::DATA_FAMILY())
            .arg(AMBER(), TEXT_ON_ACCENT(), TEXT_PRIMARY(), BG_RAISED(), BG_HOVER(), BORDER_BRIGHT(), AMBER_DIM(),
                 NEGATIVE()));
    header_bar_->setObjectName(QStringLiteral("mkHeader"));
}

} // namespace finterm::screens
