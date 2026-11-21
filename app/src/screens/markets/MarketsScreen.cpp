#include "screens/markets/MarketsScreen.h"

#include "screens/markets/MarketChartDialog.h"
#include "screens/markets/MarketPanelEditor.h"
#include "screens/markets/MarketPanelStore.h"
#include "screens/markets/conditions/ConditionsView.h"
#include "screens/markets/world/WorldMarketsView.h"
#include "services/markets/MarketDataService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTimeZone>
#include <QVBoxLayout>

namespace finterm::screens {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

namespace {
/// The PANELS / WORLD choice. Not "markets_view_mode": see the constructor.
constexpr const char* kViewModeKey = "markets_view";
constexpr int kPanelsPage = 0;
constexpr int kWorldPage = 1;
constexpr int kConditionsPage = 2;
} // namespace

static QString lbl_ss(const QString& color, bool bold = false, int px = -1) {
    const int sz = (px > 0) ? px : ui::fonts::font_px(-2);
    return QString("color:%1;background:transparent;font-size:%2px;font-family:'%3';%4")
        .arg(color)
        .arg(sz)
        .arg(ui::fonts::DATA_FAMILY())
        .arg(bold ? "font-weight:bold;" : "");
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MarketsScreen::MarketsScreen(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    header_bar_ = build_header_bar();
    root->addWidget(header_bar_);

    stack_ = new QStackedWidget(this);
    panels_page_ = new QWidget(stack_);
    auto* pl = new QVBoxLayout(panels_page_);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    configs_ = MarketPanelStore::instance().load();
    build_splitter_layout();
    pl->addWidget(h_splitter_, 1);
    restore_splitter_state();
    stack_->addWidget(panels_page_); // kPanelsPage

    world_ = new world::WorldMarketsView(stack_);
    connect(world_, &world::WorldMarketsView::symbol_activated, this,
            [this](const QString& sym, const QString& name) { MarketChartDialog::show_for(sym, name, this); });
    stack_->addWidget(world_); // kWorldPage
    root->addWidget(stack_, 1);

    auto_refresh_timer_ = new QTimer(this);
    auto_refresh_timer_->setInterval(update_interval_ms_);
    connect(auto_refresh_timer_, &QTimer::timeout, this, &MarketsScreen::refresh_all);

    refresh_timeout_ = new QTimer(this);
    refresh_timeout_->setSingleShot(true);
    refresh_timeout_->setInterval(kRefreshTimeoutMs);
    connect(refresh_timeout_, &QTimer::timeout, this, [this]() {
        if (!refresh_in_progress_)
            return;
        refresh_in_progress_ = false;
        pending_refreshes_ = 0;
        if (status_label_) {
            status_label_->setText("● TIMEOUT");
            status_label_->setStyleSheet(lbl_ss(ui::colors::NEGATIVE(), true));
        }
    });

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
    refresh_theme();

    // PANELS unless the user last chose WORLD. The choice lives under a new
    // key: "markets_view_mode" held "world" for everyone for a day only
    // because WORLD was the forced default — it is not a choice to honour.
    auto& settings = SettingsRepository::instance();
    if (const auto old = settings.get("markets_view_mode"); old.is_ok() && !old.value().isEmpty())
        settings.remove("markets_view_mode");
    const auto saved = settings.get(kViewModeKey);
    const QString saved_mode = saved.is_ok() ? saved.value() : QString();
    set_mode(saved_mode == QLatin1String("world")        ? Mode::World
             : saved_mode == QLatin1String("conditions") ? Mode::Conditions
                                                         : Mode::Panels,
             false);
}

// ---------------------------------------------------------------------------
// Mode (WORLD overview / user PANELS)
// ---------------------------------------------------------------------------

void MarketsScreen::ensure_conditions() {
    if (conditions_)
        return;
    // Built on first use: twelve widgets nobody may ever open stay unbuilt.
    conditions_ = new ConditionsView(stack_);
    stack_->insertWidget(kConditionsPage, conditions_);
    if (cond_add_slot_) {
        cond_add_slot_->addWidget(conditions_->add_button());
        style_add_button();
    }
}

void MarketsScreen::style_add_button() {
    if (!conditions_)
        return;
    conditions_->add_button()->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %1;font-size:%2px;font-family:'%3';"
                "font-weight:700;padding:0 8px;}")
            .arg(ui::colors::AMBER())
            .arg(ui::fonts::font_px(-3))
            .arg(ui::fonts::DATA_FAMILY()));
}

void MarketsScreen::set_mode(Mode m, bool persist) {
    if (m == Mode::Conditions)
        ensure_conditions();
    const int page = m == Mode::World ? kWorldPage : m == Mode::Conditions ? kConditionsPage : kPanelsPage;
    const bool changed = m != mode_ || stack_->currentIndex() != page;
    mode_ = m;
    // Switching pages hides the other one, so WORLD unsubscribes and stops
    // its timers by itself (WorldMarketsView::hideEvent).
    stack_->setCurrentIndex(page);
    for (QWidget* w : panel_only_controls_)
        w->setVisible(m == Mode::Panels);
    if (conditions_)
        conditions_->add_button()->setVisible(m == Mode::Conditions);
    update_mode_buttons();
    if (persist)
        SettingsRepository::instance().set(kViewModeKey, m == Mode::World        ? "world"
                                                         : m == Mode::Conditions ? "conditions"
                                                                                 : "panels");
    if (!changed || !isVisible())
        return;
    if (m == Mode::Panels) {
        if (auto_update_)
            auto_refresh_timer_->start();
        refresh_panels();
    } else {
        auto_refresh_timer_->stop();
        save_splitter_state();
        // The hidden panels stop holding their quote topics (refresh_panels
        // subscribes them again on the way back).
        for (auto* p : panels_)
            p->hub_unsubscribe_all();
    }
}

void MarketsScreen::update_mode_buttons() {
    auto style = [](QPushButton* b, bool on) {
        if (!b)
            return;
        b->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                                 "font-size:%4px;font-family:'%5';font-weight:700;padding:0 8px;}"
                                 "QPushButton:hover{color:%6;}")
                             .arg(on ? ui::colors::AMBER() : QString("transparent"),
                                  on ? ui::colors::TEXT_ON_ACCENT() : ui::colors::TEXT_SECONDARY(),
                                  on ? ui::colors::AMBER() : ui::colors::BORDER_MED())
                             .arg(ui::fonts::font_px(-3))
                             .arg(ui::fonts::DATA_FAMILY())
                             .arg(on ? ui::colors::TEXT_ON_ACCENT() : ui::colors::TEXT_PRIMARY()));
        b->setChecked(on);
    };
    style(world_btn_, mode_ == Mode::World);
    style(panels_btn_, mode_ == Mode::Panels);
    style(conditions_btn_, mode_ == Mode::Conditions);
    style_add_button();
}

// ---------------------------------------------------------------------------
// Splitter layout
// ---------------------------------------------------------------------------

void MarketsScreen::build_splitter_layout() {
    h_splitter_ = new QSplitter(Qt::Horizontal, this);
    h_splitter_->setHandleWidth(3);
    h_splitter_->setChildrenCollapsible(false);

    col_splitters_.clear();
    panels_.clear();

    for (int col = 0; col < kNumColumns; ++col) {
        auto* vs = new QSplitter(Qt::Vertical);
        vs->setHandleWidth(3);
        vs->setChildrenCollapsible(false);
        col_splitters_.append(vs);
        h_splitter_->addWidget(vs);
    }

    // Place panels into their designated columns
    for (const auto& cfg : configs_) {
        int col = qBound(0, cfg.column_index, kNumColumns - 1);
        auto* p = new MarketPanel(cfg, col_splitters_[col]);
        panels_.append(p);
        col_splitters_[col]->addWidget(p);
        wire_panel(p);
    }

    // Ensure empty columns have a placeholder so they don't collapse
    for (int col = 0; col < kNumColumns; ++col) {
        bool has_panel = false;
        for (int j = 0; j < col_splitters_[col]->count(); ++j) {
            if (qobject_cast<MarketPanel*>(col_splitters_[col]->widget(j))) {
                has_panel = true;
                break;
            }
        }
        if (!has_panel) {
            auto* placeholder = new QWidget;
            placeholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            col_splitters_[col]->addWidget(placeholder);
        }
    }

    // Distribute equal height to every panel in each column so no single row
    // grabs disproportionate space.  (Restored saved state may override this.)
    for (auto* vs : col_splitters_) {
        const int n = vs->count();
        if (n > 1) {
            const int equal = 1000; // arbitrary equal weight
            QList<int> sizes;
            sizes.reserve(n);
            for (int i = 0; i < n; ++i)
                sizes.append(equal);
            vs->setSizes(sizes);
        }
    }

    refresh_theme();
}

void MarketsScreen::wire_panel(MarketPanel* p) {
    connect(p, &MarketPanel::edit_requested, this, &MarketsScreen::open_editor);
    connect(p, &MarketPanel::delete_requested, this, &MarketsScreen::on_panel_delete);
    connect(p, &MarketPanel::config_changed, this, &MarketsScreen::on_panel_config_changed);
    connect(p, &MarketPanel::symbol_activated, this,
            [this](const QString& sym, const QString& name) { MarketChartDialog::show_for(sym, name, this); });
}

void MarketsScreen::rebuild_splitter_layout() {
    if (h_splitter_) {
        h_splitter_->setParent(nullptr);
        h_splitter_->deleteLater();
        h_splitter_ = nullptr;
    }
    panels_.clear();
    col_splitters_.clear();

    build_splitter_layout();

    panels_page_->layout()->addWidget(h_splitter_);
    restore_splitter_state();
}

int MarketsScreen::column_with_fewest_panels() const {
    int best = 0, count = INT_MAX;
    for (int i = 0; i < kNumColumns; ++i) {
        int c = 0;
        for (int j = 0; j < col_splitters_[i]->count(); ++j) {
            if (qobject_cast<MarketPanel*>(col_splitters_[i]->widget(j)))
                ++c;
        }
        if (c < count) {
            count = c;
            best = i;
        }
    }
    return best;
}

void MarketsScreen::save_splitter_state() {
    if (!h_splitter_)
        return;
    // Save only horizontal splitter state (column widths).
    // Vertical row heights are always equal-distributed on build.
    const QString state = QString::fromLatin1(h_splitter_->saveState().toBase64());
    SettingsRepository::instance().set("markets_splitter_state", state);
}

void MarketsScreen::restore_splitter_state() {
    if (!h_splitter_)
        return;
    auto res = SettingsRepository::instance().get("markets_splitter_state");
    if (!res.is_ok() || res.value().isEmpty())
        return;

    const QString saved = res.value();
    int sep = saved.indexOf('|');
    // Restore only the horizontal splitter (column widths).
    // Vertical (row heights) are set to equal in build_splitter_layout()
    // and must not be overridden by stale saved state.
    if (sep < 0) {
        h_splitter_->restoreState(QByteArray::fromBase64(saved.toLatin1()));
        return;
    }
    h_splitter_->restoreState(QByteArray::fromBase64(saved.left(sep).toLatin1()));
}

// ---------------------------------------------------------------------------
// Panel management
// ---------------------------------------------------------------------------

void MarketsScreen::open_editor(const QString& panel_id) {
    MarketPanelConfig cfg;
    int target_col = column_with_fewest_panels();
    if (!panel_id.isEmpty()) {
        for (const auto& c : configs_) {
            if (c.id == panel_id) {
                cfg = c;
                target_col = c.column_index;
                break;
            }
        }
    }

    auto* dlg = new MarketPanelEditor(cfg, this);
    if (dlg->exec() != QDialog::Accepted) {
        dlg->deleteLater();
        return;
    }
    MarketPanelConfig updated = dlg->result_config();
    dlg->deleteLater();
    updated.column_index = target_col;

    if (panel_id.isEmpty()) {
        configs_.append(updated);
    } else {
        for (auto& c : configs_) {
            if (c.id == panel_id) {
                c = updated;
                break;
            }
        }
    }

    MarketPanelStore::instance().save(configs_);
    rebuild_splitter_layout();
    refresh_all();
}

void MarketsScreen::open_editor_for_new_panel(int col_index) {
    MarketPanelConfig cfg;
    auto* dlg = new MarketPanelEditor(cfg, this);
    if (dlg->exec() != QDialog::Accepted) {
        dlg->deleteLater();
        return;
    }
    MarketPanelConfig updated = dlg->result_config();
    dlg->deleteLater();
    updated.column_index = col_index;
    configs_.append(updated);
    MarketPanelStore::instance().save(configs_);
    rebuild_splitter_layout();
    refresh_all();
}

void MarketsScreen::on_panel_delete(const QString& panel_id) {
    QMessageBox mb(this);
    mb.setWindowTitle("Remove Panel");
    mb.setText("Remove this panel?");
    mb.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    mb.setDefaultButton(QMessageBox::Cancel);
    mb.setStyleSheet(QString("background:%1;color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
    if (mb.exec() != QMessageBox::Yes)
        return;

    configs_.erase(
        std::remove_if(configs_.begin(), configs_.end(), [&](const MarketPanelConfig& c) { return c.id == panel_id; }),
        configs_.end());
    MarketPanelStore::instance().save(configs_);
    rebuild_splitter_layout();
}

void MarketsScreen::on_panel_config_changed(const MarketPanelConfig& cfg) {
    for (auto& c : configs_) {
        if (c.id == cfg.id) {
            c = cfg;
            break;
        }
    }
    MarketPanelStore::instance().save(configs_);
}

// ---------------------------------------------------------------------------
// Header bar (single 36px strip)
// ---------------------------------------------------------------------------

QWidget* MarketsScreen::build_header_bar() {
    auto* w = new QWidget(this);
    w->setFixedHeight(36);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(0);

    auto add_sep = [&]() {
        auto* s = new QLabel("|");
        s->setStyleSheet(lbl_ss(ui::colors::BORDER_DIM()));
        s->setContentsMargins(10, 0, 10, 0);
        h->addWidget(s);
    };

    // Branding
    auto* brand = new QLabel("FINTERM MARKETS");
    brand->setStyleSheet(lbl_ss(ui::colors::TEXT_PRIMARY(), true));
    h->addWidget(brand);

    add_sep();

    // PANELS / WORLD — two-state toggle; the choice is remembered.
    panels_btn_ = new QPushButton("PANELS");
    world_btn_ = new QPushButton("WORLD");
    conditions_btn_ = new QPushButton("CONDITIONS");
    for (auto* b : {panels_btn_, world_btn_, conditions_btn_}) {
        b->setFixedHeight(22);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        h->addWidget(b);
    }
    panels_btn_->setToolTip("Your configurable quote panels.");
    connect(world_btn_, &QPushButton::clicked, this, [this]() { set_mode(Mode::World, true); });
    connect(panels_btn_, &QPushButton::clicked, this, [this]() { set_mode(Mode::Panels, true); });
    conditions_btn_->setToolTip("Market conditions: trend, 200-day, percentiles, yield curve, credit, macro, "
                                "correlation and prediction-market odds — the same widgets the dashboard offers.");
    connect(conditions_btn_, &QPushButton::clicked, this, [this]() { set_mode(Mode::Conditions, true); });
    // The CONDITIONS page's "+ WIDGET" sits here (built with the page).
    auto* slot_box = new QWidget;
    cond_add_slot_ = new QHBoxLayout(slot_box);
    cond_add_slot_->setContentsMargins(6, 0, 0, 0);
    h->addWidget(slot_box);

    add_sep();

    // Session status + clocks. PANELS only: WORLD has its own sessions strip.
    auto* clocks_box = new QWidget;
    auto* ch = new QHBoxLayout(clocks_box);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(0);
    auto add_box_sep = [&]() {
        auto* s = new QLabel("|");
        s->setStyleSheet(lbl_ss(ui::colors::BORDER_DIM()));
        s->setContentsMargins(10, 0, 10, 0);
        ch->addWidget(s);
    };
    session_label_ = new QLabel;
    session_label_->setStyleSheet(lbl_ss(ui::colors::TEXT_SECONDARY(), true, 11));
    ch->addWidget(session_label_);
    update_session_status();

    session_timer_ = new QTimer(this);
    session_timer_->setInterval(60000);
    connect(session_timer_, &QTimer::timeout, this, &MarketsScreen::update_session_status);

    add_box_sep();

    // Clocks
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
    update_clocks();

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &MarketsScreen::update_clocks);

    add_box_sep();
    h->addWidget(clocks_box);

    // Controls — all uniform dim style
    auto make_ctrl_btn = [&](const QString& label) -> QPushButton* {
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
    connect(refresh_btn, &QPushButton::clicked, this, &MarketsScreen::refresh_all);
    h->addWidget(refresh_btn);

    // AUTO toggle — amber when ON
    auto* auto_btn = make_ctrl_btn(auto_update_ ? "[F9] AUTO: ON" : "[F9] AUTO: OFF");
    auto update_auto_style = [this, auto_btn]() {
        auto_btn->setText(auto_update_ ? "[F9] AUTO: ON" : "[F9] AUTO: OFF");
        auto_btn->setStyleSheet(
            QString("QPushButton{background:transparent;color:%1;border:none;"
                    "font-size:%3px;font-family:'%4';padding:0 6px;}"
                    "QPushButton:hover{color:%2;}")
                .arg(auto_update_ ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY(), ui::colors::TEXT_PRIMARY())
                .arg(ui::fonts::font_px(-3))
                .arg(ui::fonts::DATA_FAMILY()));
    };
    update_auto_style();
    connect(auto_btn, &QPushButton::clicked, this, [this, update_auto_style]() {
        auto_update_ = !auto_update_;
        update_auto_style();
        if (!isVisible() || mode_ != Mode::Panels)
            return;
        if (auto_update_)
            auto_refresh_timer_->start();
        else
            auto_refresh_timer_->stop();
    });
    h->addWidget(auto_btn);

    // Interval combo
    auto* iv = new QComboBox;
    iv->setFixedHeight(24);
    iv->setFixedWidth(56);
    iv->addItem("5M", 300000);
    iv->addItem("10M", 600000);
    iv->addItem("15M", 900000);
    iv->addItem("30M", 1800000);
    iv->addItem("1H", 3600000);
    iv->addItem("4H", 14400000);
    iv->addItem("1D", 86400000);
    iv->setCurrentIndex(1);
    iv->setStyleSheet(QString("QComboBox{background:transparent;color:%1;border:none;"
                              "font-size:%6px;font-family:'%7';padding:0 4px;}"
                              "QComboBox::drop-down{border:none;width:12px;}"
                              "QComboBox QAbstractItemView{background:%2;color:%3;border:1px solid %4;"
                              "selection-background-color:%5;font-size:%6px;font-family:'%7';}")
                          .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(),
                               ui::colors::BORDER_MED(), ui::colors::BG_HOVER())
                          .arg(ui::fonts::font_px(-3))
                          .arg(ui::fonts::DATA_FAMILY()));
    connect(iv, &QComboBox::currentIndexChanged, this, [this, iv](int i) {
        update_interval_ms_ = iv->itemData(i).toInt();
        auto_refresh_timer_->setInterval(update_interval_ms_);
    });
    h->addWidget(iv);

    // [+] PANEL — column picker menu
    panel_only_controls_ = {clocks_box, auto_btn, iv};
    auto* add_btn = make_ctrl_btn("[+] PANEL");
    connect(add_btn, &QPushButton::clicked, this, [this, add_btn]() {
        auto* menu = new QMenu(this);
        menu->setStyleSheet(QString("QMenu{background:%1;border:1px solid %2;color:%3;"
                                    "font-size:%5px;font-family:'%6';}"
                                    "QMenu::item{padding:4px 16px;}"
                                    "QMenu::item:selected{background:%4;}")
                                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED(), ui::colors::TEXT_PRIMARY(),
                                     ui::colors::BG_HOVER())
                                .arg(ui::fonts::font_px(-3))
                                .arg(ui::fonts::DATA_FAMILY()));
        for (int i = 0; i < kNumColumns; ++i) {
            auto* act = menu->addAction(QString("ADD TO COL %1").arg(i + 1));
            connect(act, &QAction::triggered, this, [this, i]() { open_editor_for_new_panel(i); });
        }
        menu->exec(add_btn->mapToGlobal(QPoint(0, add_btn->height())));
        menu->deleteLater();
    });
    h->addWidget(add_btn);
    panel_only_controls_.append(add_btn);

    // RESET
    auto* reset_btn = make_ctrl_btn("RESET");
    connect(reset_btn, &QPushButton::clicked, this, [this]() {
        QMessageBox mb(this);
        mb.setWindowTitle("Reset Panels");
        mb.setText("Reset all panels to defaults?");
        mb.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        mb.setStyleSheet(QString("background:%1;color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
        if (mb.exec() != QMessageBox::Yes)
            return;
        MarketPanelStore::instance().reset_to_defaults();
        configs_ = MarketPanelStore::instance().load();
        rebuild_splitter_layout();
        refresh_all();
    });
    h->addWidget(reset_btn);
    panel_only_controls_.append(reset_btn);

    h->addStretch();

    last_upd_label_ = new QLabel("LAST UPDATE  --:--:--");
    last_upd_label_->setStyleSheet(lbl_ss(ui::colors::TEXT_SECONDARY(), false, 11));
    h->addWidget(last_upd_label_);
    panel_only_controls_.append(last_upd_label_); // WORLD shows its own "quotes HH:mm:ss"

    h->addWidget(new QLabel("   "));

    status_label_ = new QLabel("● READY");
    status_label_->setStyleSheet(lbl_ss(ui::colors::POSITIVE(), true));
    h->addWidget(status_label_);

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
// Show / hide — timer lifecycle (P3)
// ---------------------------------------------------------------------------

void MarketsScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    // Distribute equal space now that geometry is valid.
    // Horizontal: equal column widths.
    if (h_splitter_ && h_splitter_->count() > 1) {
        QList<int> hsizes;
        for (int i = 0; i < h_splitter_->count(); ++i)
            hsizes.append(1000);
        h_splitter_->setSizes(hsizes);
    }
    // Vertical: equal panel heights within each column.
    for (auto* vs : col_splitters_) {
        if (vs->count() > 1) {
            QList<int> vsizes;
            for (int i = 0; i < vs->count(); ++i)
                vsizes.append(1000);
            vs->setSizes(vsizes);
        }
    }

    if (auto_update_ && auto_refresh_timer_ && mode_ == Mode::Panels)
        auto_refresh_timer_->start();
    if (session_timer_)
        session_timer_->start();
    if (clock_timer_)
        clock_timer_->start();
    // User just brought MARKETS to the front — force-fresh by invalidating
    // ONLY this screen's panel symbols. The old code passed {} which nukes
    // the entire "market:*" cache, including symbols other screens
    // (Portfolio holdings, dashboard widgets) had just cached. After a
    // Markets→Portfolio→Markets round-trip every quote was re-fetched
    // unnecessarily; with scoped invalidation each screen only drops its
    // own cohort. The DataHub min_interval (2s) still rate-limits actual
    // outbound calls. WORLD manages its own subscriptions (WorldMarketsView).
    if (mode_ != Mode::Panels)
        return;
    QStringList panel_syms;
    for (auto* p : panels_) {
        for (const QString& s : p->symbols()) {
            if (!panel_syms.contains(s))
                panel_syms.append(s);
        }
    }
    if (!panel_syms.isEmpty())
        services::MarketDataService::instance().invalidate_quotes(panel_syms);
    refresh_all();
}

void MarketsScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (auto_refresh_timer_)
        auto_refresh_timer_->stop();
    if (session_timer_)
        session_timer_->stop();
    if (clock_timer_)
        clock_timer_->stop();
    save_splitter_state();
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void MarketsScreen::refresh_all() {
    if (mode_ == Mode::Conditions) {
        if (conditions_)
            conditions_->refresh_all();
        return;
    }
    if (mode_ == Mode::World) {
        if (world_)
            world_->refresh(/*force=*/true);
        return;
    }
    refresh_panels();
}

void MarketsScreen::refresh_panels() {
    if (refresh_in_progress_)
        return;
    if (panels_.isEmpty())
        return;
    refresh_in_progress_ = true;
    pending_refreshes_ = panels_.size();

    if (status_label_) {
        status_label_->setText("● LOADING");
        status_label_->setStyleSheet(lbl_ss(ui::colors::AMBER(), true));
    }

    refresh_timeout_->start();

    auto* counter = new QObject(this);
    for (auto* p : panels_) {
        p->refresh();
        connect(
            p, &MarketPanel::refresh_finished, counter,
            [this, counter]() {
                if (--pending_refreshes_ > 0)
                    return;
                refresh_timeout_->stop();
                refresh_in_progress_ = false;
                counter->deleteLater();
                if (status_label_) {
                    status_label_->setText("● READY");
                    status_label_->setStyleSheet(lbl_ss(ui::colors::POSITIVE(), true));
                }
                if (last_upd_label_) {
                    last_upd_label_->setText(
                        QString("LAST UPDATE  %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
                    last_upd_label_->setStyleSheet(lbl_ss(ui::colors::TEXT_SECONDARY(), false, 11));
                }
            },
            Qt::SingleShotConnection);
    }
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void MarketsScreen::refresh_theme() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    const QString handle_ss = QString("QSplitter::handle{background:%1;}").arg(ui::colors::BORDER_DIM());

    if (header_bar_)
        header_bar_->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;")
                                       .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    if (h_splitter_)
        h_splitter_->setStyleSheet(handle_ss);

    for (auto* vs : col_splitters_)
        vs->setStyleSheet(handle_ss);

    update_mode_buttons();
}

} // namespace finterm::screens
