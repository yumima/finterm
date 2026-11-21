#include "screens/markets/MarketsScreen.h"

#include "core/events/EventBus.h"
#include "screens/futures/FuturesScreen.h"
#include "screens/markets/MarketChartDialog.h"
#include "screens/markets/overview/MarketsOverview.h"
#include "screens/markets/world/WorldMarketsView.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {
/// The page last used. Was PANELS / WORLD / CONDITIONS: PANELS and CONDITIONS
/// became OVERVIEW, so an old "panels" or "conditions" opens OVERVIEW.
constexpr const char* kViewModeKey = "markets_view";
constexpr int kOverviewPage = 0;
constexpr int kFuturesPage = 1;
constexpr int kWorldPage = 2;

QString page_key(MarketsScreen::Page p) {
    switch (p) {
        case MarketsScreen::Page::Futures:
            return QStringLiteral("futures");
        case MarketsScreen::Page::World:
            return QStringLiteral("world");
        case MarketsScreen::Page::Overview:
            break;
    }
    return QStringLiteral("overview");
}

MarketsScreen::Page page_from(const QString& k) {
    if (k == QLatin1String("futures"))
        return MarketsScreen::Page::Futures;
    if (k == QLatin1String("world"))
        return MarketsScreen::Page::World;
    return MarketsScreen::Page::Overview;
}
} // namespace

static QString lbl_ss(const QString& color, bool bold = false, int px = -1) {
    const int sz = (px > 0) ? px : ui::fonts::font_px(-2);
    return QString("color:%1;background:transparent;font-size:%2px;font-family:'%3';%4")
        .arg(color)
        .arg(sz)
        .arg(ui::fonts::DATA_FAMILY())
        .arg(bold ? "font-weight:bold;" : "");
}

MarketsScreen::MarketsScreen(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    header_bar_ = build_header_bar();
    root->addWidget(header_bar_);

    stack_ = new QStackedWidget(this);
    overview_ = new MarketsOverview(stack_);
    stack_->addWidget(overview_); // kOverviewPage
    futures_slot_ = new QWidget(stack_);
    stack_->addWidget(futures_slot_); // kFuturesPage — FuturesScreen replaces it on first use
    world_ = new world::WorldMarketsView(stack_);
    connect(world_, &world::WorldMarketsView::symbol_activated, this,
            [this](const QString& sym, const QString& name) { MarketChartDialog::show_for(sym, name, this); });
    stack_->addWidget(world_); // kWorldPage
    root->addWidget(stack_, 1);

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();

    auto& settings = SettingsRepository::instance();
    const auto saved = settings.get(kViewModeKey);
    set_page(page_from(saved.is_ok() ? saved.value() : QString()), false);

    live_ = this;
    if (pending_request_) { // asked for before this screen existed
        pending_request_ = false;
        open_futures(pending_root_);
        pending_root_.clear();
    }
}

QPointer<MarketsScreen> MarketsScreen::live_;
bool MarketsScreen::pending_request_ = false;
QString MarketsScreen::pending_root_;

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

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void MarketsScreen::ensure_futures() {
    if (futures_)
        return;
    // Built on first use: the futures board, curve and COT stay unbuilt for a
    // reader who never opens them.
    futures_ = new FuturesScreen(stack_);
    stack_->insertWidget(kFuturesPage, futures_);
    stack_->removeWidget(futures_slot_);
    futures_slot_->deleteLater();
    futures_slot_ = nullptr;
    if (!pending_futures_state_.isEmpty()) {
        futures_->restore_state(pending_futures_state_);
        pending_futures_state_.clear();
    }
}

void MarketsScreen::set_page(Page p, bool persist) {
    if (p == Page::Futures)
        ensure_futures();
    page_ = p;
    stack_->setCurrentIndex(p == Page::Futures ? kFuturesPage : p == Page::World ? kWorldPage : kOverviewPage);
    for (QWidget* w : overview_only_)
        w->setVisible(p == Page::Overview);
    if (clocks_box_)
        clocks_box_->setVisible(p != Page::World); // WORLD has its own sessions strip
    update_page_buttons();
    if (persist)
        SettingsRepository::instance().set(kViewModeKey, page_key(p));
}

void MarketsScreen::open_futures(const QString& root) {
    set_page(Page::Futures);
    if (!root.isEmpty())
        futures_->open_market(root);
}


void MarketsScreen::update_page_buttons() {
    auto style = [](QPushButton* b, bool on) {
        if (!b)
            return;
        b->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                                 "font-size:%4px;font-family:'%5';font-weight:700;padding:0 10px;}"
                                 "QPushButton:hover{color:%6;}")
                             .arg(on ? ui::colors::AMBER() : QString("transparent"),
                                  on ? ui::colors::TEXT_ON_ACCENT() : ui::colors::TEXT_SECONDARY(),
                                  on ? ui::colors::AMBER() : ui::colors::BORDER_MED())
                             .arg(ui::fonts::font_px(-3))
                             .arg(ui::fonts::DATA_FAMILY())
                             .arg(on ? ui::colors::TEXT_ON_ACCENT() : ui::colors::TEXT_PRIMARY()));
        b->setChecked(on);
    };
    style(overview_btn_, page_ == Page::Overview);
    style(futures_btn_, page_ == Page::Futures);
    style(world_btn_, page_ == Page::World);
}

// ---------------------------------------------------------------------------
// State (the dock router keeps it per screen)
// ---------------------------------------------------------------------------

void MarketsScreen::restore_state(const QVariantMap& state) {
    const QVariantMap fut = state.value(QStringLiteral("futures")).toMap();
    if (futures_)
        futures_->restore_state(fut);
    else
        pending_futures_state_ = fut;
}

QVariantMap MarketsScreen::save_state() const {
    QVariantMap m{{QStringLiteral("page"), page_key(page_)}};
    if (futures_)
        m.insert(QStringLiteral("futures"), futures_->save_state());
    else if (!pending_futures_state_.isEmpty())
        m.insert(QStringLiteral("futures"), pending_futures_state_);
    return m;
}

// ---------------------------------------------------------------------------
// Header bar
// ---------------------------------------------------------------------------

QWidget* MarketsScreen::build_header_bar() {
    auto* w = new QWidget(this);
    w->setFixedHeight(36);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(0);

    auto add_sep = [](QHBoxLayout* l) {
        auto* s = new QLabel("|");
        s->setStyleSheet(lbl_ss(ui::colors::BORDER_DIM()));
        s->setContentsMargins(10, 0, 10, 0);
        l->addWidget(s);
    };

    auto* brand = new QLabel("FINTERM MARKETS");
    brand->setStyleSheet(lbl_ss(ui::colors::TEXT_PRIMARY(), true));
    h->addWidget(brand);
    add_sep(h);

    overview_btn_ = new QPushButton("OVERVIEW");
    futures_btn_ = new QPushButton("FUTURES");
    world_btn_ = new QPushButton("WORLD");
    for (auto* b : {overview_btn_, futures_btn_, world_btn_}) {
        b->setFixedHeight(22);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        h->addWidget(b);
    }
    overview_btn_->setToolTip("Your canvas: quote lists, market conditions and a chart linked to them. Drag, resize, "
                              "+ WIDGET to add, a tile's ✕ to remove; each list edits in place (+ ADD / −).");
    futures_btn_->setToolTip("One futures market at a time: board, chart, forward curve, contract months, spreads, "
                             "positioning and the ticket.");
    world_btn_->setToolTip("Global markets on one screen: sessions, regions, rates, FX, commodities, crypto, vol.");
    connect(overview_btn_, &QPushButton::clicked, this, [this]() { set_page(Page::Overview); });
    connect(futures_btn_, &QPushButton::clicked, this, [this]() { set_page(Page::Futures); });
    connect(world_btn_, &QPushButton::clicked, this, [this]() { set_page(Page::World); });

    add_sep(h);

    // Session status + clocks (WORLD has its own sessions strip).
    clocks_box_ = new QWidget;
    auto* ch = new QHBoxLayout(clocks_box_);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(0);
    session_label_ = new QLabel;
    session_label_->setStyleSheet(lbl_ss(ui::colors::TEXT_SECONDARY(), true, 11));
    ch->addWidget(session_label_);
    add_sep(ch);
    ny_label_ = new QLabel;
    lon_label_ = new QLabel;
    tok_label_ = new QLabel;
    for (auto* lbl : {ny_label_, lon_label_, tok_label_})
        lbl->setStyleSheet(lbl_ss(ui::colors::TEXT_SECONDARY(), false, 11));
    ch->addWidget(ny_label_);
    ch->addWidget(new QLabel("   "));
    ch->addWidget(lon_label_);
    ch->addWidget(new QLabel("   "));
    ch->addWidget(tok_label_);
    add_sep(ch);
    h->addWidget(clocks_box_);
    update_session_status();
    update_clocks();
    session_timer_ = new QTimer(this);
    session_timer_->setInterval(60000);
    connect(session_timer_, &QTimer::timeout, this, &MarketsScreen::update_session_status);
    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &MarketsScreen::update_clocks);

    auto make_ctrl_btn = [](const QString& label) -> QPushButton* {
        auto* b = new QPushButton(label);
        b->setFixedHeight(24);
        b->setCursor(Qt::PointingHandCursor);
        b->setFlat(true);
        b->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:none;"
                                 "font-size:%2px;font-family:'%3';padding:0 6px;}"
                                 "QPushButton:hover{color:%4;}")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::font_px(-3))
                             .arg(ui::fonts::DATA_FAMILY())
                             .arg(ui::colors::TEXT_PRIMARY()));
        return b;
    };

    auto* refresh_btn = make_ctrl_btn("[F5] REFRESH");
    refresh_btn->setToolTip("Ask every list, chart and reading on this page for a fresh answer now. They also "
                            "refresh on their own while on screen.");
    connect(refresh_btn, &QPushButton::clicked, this, &MarketsScreen::refresh_all);
    h->addWidget(refresh_btn);

    auto* add_btn = make_ctrl_btn("+ WIDGET");
    add_btn->setToolTip("Add a widget to the canvas: a Quote List, a market-condition widget, a linked chart, "
                        "or any dashboard widget");
    connect(add_btn, &QPushButton::clicked, this, [this]() { overview_->add_widget_dialog(); });
    h->addWidget(add_btn);
    auto* reset_btn = make_ctrl_btn("RESET");
    reset_btn->setToolTip("Back to the default canvas (your old panels, the condition widgets and a chart)");
    connect(reset_btn, &QPushButton::clicked, this, [this]() {
        QMessageBox mb(this);
        mb.setWindowTitle("Reset Overview");
        mb.setText("Reset the OVERVIEW canvas to its default layout? Its tiles and their lists are replaced.");
        mb.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        mb.setStyleSheet(QString("background:%1;color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
        if (mb.exec() == QMessageBox::Yes)
            overview_->reset_layout();
    });
    h->addWidget(reset_btn);
    overview_only_ = {add_btn, reset_btn};

    h->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
// Session status & clocks — DST-correct via QTimeZone
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

void MarketsScreen::update_clocks() {
    QDateTime utc = QDateTime::currentDateTimeUtc();
    if (ny_label_)
        ny_label_->setText(QString("NY %1").arg(utc.toTimeZone(QTimeZone("America/New_York")).toString("HH:mm:ss")));
    if (lon_label_)
        lon_label_->setText(QString("LON %1").arg(utc.toTimeZone(QTimeZone("Europe/London")).toString("HH:mm:ss")));
    if (tok_label_)
        tok_label_->setText(QString("TOK %1").arg(utc.toTimeZone(QTimeZone("Asia/Tokyo")).toString("HH:mm:ss")));
}

// ---------------------------------------------------------------------------
// Show / hide — timer lifecycle
// ---------------------------------------------------------------------------

void MarketsScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    update_session_status();
    update_clocks();
    session_timer_->start();
    clock_timer_->start();
}

void MarketsScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    session_timer_->stop();
    clock_timer_->stop();
}

void MarketsScreen::refresh_all() {
    switch (page_) {
        case Page::Overview:
            overview_->refresh_all();
            break;
        case Page::World:
            world_->refresh(/*force=*/true);
            break;
        case Page::Futures:
            break; // the futures page refreshes its board, curve and chart on its own timers
    }
}

void MarketsScreen::refresh_theme() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    if (header_bar_)
        header_bar_->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                                       .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    update_page_buttons();
}

} // namespace finterm::screens
