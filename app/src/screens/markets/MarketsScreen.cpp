#include "screens/markets/MarketsScreen.h"

#include "core/events/EventBus.h"
#include "screens/dashboard/canvas/LayoutCodec.h"
#include "screens/dashboard/widgets/ConditionWidgets.h"
#include "screens/markets/world/WorldMarketsModel.h"
#include "screens/markets/overview/MarketScreenPage.h"
#include "screens/markets/overview/MarketTemplates.h"
#include "services/futures/FuturesCatalog.h"
#include "services/markets/MarketDataService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QFrame>
#include <QHash>
#include <QSet>
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
constexpr const char* kScreensKey = "markets_screens";        ///< the user's screens: [{id, title, template}]
constexpr const char* kCurrentKey = "markets_screen_current"; ///< the page last shown (id)
constexpr const char* kPagesVersionKey = "markets_pages_version";
constexpr int kPagesVersion = 3; ///< 2: MY VIEW … | MARKETS EQUITY RATES WORLD | FUTURES; 3: MY VIEW the default page

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

QString layout_key(const QString& page_id) {
    return QStringLiteral("markets_screen:") + page_id;
}

bool is_market_page(const QString& id) {
    for (const auto& t : markets::market_pages())
        if (t.id == id)
            return true;
    return false;
}

QJsonArray read_mine() {
    const auto r = SettingsRepository::instance().get(kScreensKey);
    return r.is_ok() ? QJsonDocument::fromJson(r.value().toUtf8()).array() : QJsonArray();
}

void write_mine(const QJsonArray& arr) {
    SettingsRepository::instance().set(kScreensKey, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)),
                                       "markets");
}

/// "JPY=X" is Yahoo's USD/JPY; "USDEUR=X" the inverse of EUR/USD — the same
/// market either way, when telling whether a page shows a list's symbol.
QString fx_key(QString s) {
    if (s.endsWith(QLatin1String("=X")) && s.size() == 5)
        s = QStringLiteral("USD") + s;
    if (s.endsWith(QLatin1String("=X")) && s.size() == 8 && s.startsWith(QLatin1String("USD")))
        s = s.mid(3, 3) + QStringLiteral("USD=X");
    return s;
}

/// The symbols a page shows: its lists' rows (a widget's own list when it
/// keeps none) and the WORLD tiles' sections.
void collect_shown(const GridLayout& layout, QSet<QString>& out) {
    for (const auto& it : layout.items) {
        if (it.config.contains(QLatin1String("rows"))) {
            for (const auto& r : it.config.value("rows").toArray())
                out.insert(fx_key(r.toObject().value("symbol").toString()));
            continue;
        }
        for (const auto& r : widgets::condition_detail::default_rows(it.id))
            out.insert(fx_key(r.symbol));
        if (it.id.startsWith(QLatin1String("world_")))
            for (const auto& sec : world::world_sections()) {
                QString id = QStringLiteral("world_") + sec.title.toLower();
                id.replace(QLatin1Char(' '), QLatin1Char('_')).replace(QLatin1Char('-'), QLatin1Char('_'));
                if (id == it.id)
                    for (const auto& in : sec.rows)
                        out.insert(fx_key(in.symbol));
            }
    }
}

QJsonObject new_my_view() {
    const auto& t = markets::my_view_template();
    return QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"title", t.title}, {"template", t.id}};
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

void MarketsScreen::migrate_pages() {
    auto& settings = SettingsRepository::instance();
    const auto v = settings.get(kPagesVersionKey);
    const int have = v.is_ok() ? v.value().toInt() : 0;
    if (have >= kPagesVersion)
        return;
    if (have < 2)
        fold_into_three_groups();
    // 3: MARKETS opens on the user's own screen, MY VIEW (the first of
    // theirs) — once; after that it reopens where it was left.
    for (const auto& e : read_mine()) { // the first screen load_pages will show
        const QJsonObject o = e.toObject();
        const QString id = o.value("id").toString();
        if (!id.isEmpty() && !o.value("title").toString().isEmpty() && !is_market_page(id) &&
            !id.startsWith(QLatin1String("futures:"))) {
            settings.set(kCurrentKey, id, "markets");
            break;
        }
    }
    settings.set(kPagesVersionKey, QString::number(kPagesVersion), "markets");
}

void MarketsScreen::fold_into_three_groups() {
    auto& settings = SettingsRepository::instance();

    // The user's screens are what the old OVERVIEW list held besides the
    // market pages (now fixed) and PANELS (folded into the market pages'
    // widgets). "MY OVERVIEW" — the canvas of the build before screens — is
    // MY VIEW now.
    QJsonArray mine;
    for (const auto& e : read_mine()) {
        QJsonObject o = e.toObject();
        const QString id = o.value("id").toString();
        if (id.isEmpty() || o.value("title").toString().isEmpty() || is_market_page(id) ||
            id == QLatin1String("panels") || id.startsWith(QLatin1String("futures:")))
            continue;
        if (o.value("title").toString() == QLatin1String("MY OVERVIEW")) {
            // MY VIEW now — and its RESET is MY VIEW's default, not a blank page.
            o.insert("title", markets::my_view_template().title);
            o.insert("template", markets::my_view_template().id);
        }
        mine.append(o);
    }
    // Older still: the single OVERVIEW canvas, never made a screen.
    const auto old = settings.get(QStringLiteral("markets_overview_layout"));
    if (mine.isEmpty() && old.is_ok() && layout_codec::decode(old.value())) {
        QJsonObject mv = new_my_view();
        mv.insert("template", QStringLiteral("blank"));
        settings.set(layout_key(mv.value("id").toString()), old.value(), "markets");
        mine.append(mv);
    }
    if (mine.isEmpty())
        mine.append(new_my_view());

    // PANELS folds into the market pages: its lists are now the default lists
    // of the widgets that read the same markets. A panel holding a symbol
    // shown nowhere now (one the user added) is kept, on a screen of its own.
    const auto panels = settings.get(QStringLiteral("markets_panel_configs"));
    const QJsonArray panel_arr =
        panels.is_ok() ? QJsonDocument::fromJson(panels.value().toUtf8()).array() : QJsonArray();
    QSet<QString> panel_indices; // the user's index rows (Stock Indices)
    QVector<GridItem> from_configs;
    // Covered: shown on a page the user will have after this — their own
    // screens and the market pages, as saved or as they will start. The
    // single-stock lists (US, China, Europe) PANELS was filled with are let
    // go when untouched: they were never market readings.
    QSet<QString> covered;
    auto page_layout = [&](const QString& id, const QString& tmpl) {
        const auto r = settings.get(layout_key(id));
        const auto saved = r.is_ok() ? layout_codec::decode(r.value()) : std::nullopt;
        return saved ? *saved : markets::template_layout(tmpl);
    };
    for (const auto& e : mine) {
        const QJsonObject o = e.toObject();
        collect_shown(page_layout(o.value("id").toString(), o.value("template").toString()), covered);
    }
    for (const auto& t : markets::market_pages())
        collect_shown(page_layout(t.id, t.id), covered);
    for (const auto& r : services::MarketDataService::default_regional_markets())
        for (const auto& t : r.tickers)
            covered.insert(fx_key(t.symbol));
    static const QHash<QString, QString> col = {{"LAST", "last"}, {"CHG", "chg"}, {"CHG%", "pct"}, {"HIGH", "high"},
                                                {"LOW", "low"},   {"VOL", "vol"}, {"BID", "bid"},  {"ASK", "ask"}};
    for (const auto& v : panel_arr) {
        const QJsonObject p = v.toObject();
        const QString title = p.value("title").toString();
        QStringList symbols;
        for (const auto& sv : p.value("symbols").toArray())
            if (const QString sym = sv.toString().trimmed(); !sym.isEmpty())
                symbols << sym;
        if (title.compare(QLatin1String("Stock Indices"), Qt::CaseInsensitive) == 0)
            for (const auto& sym : symbols)
                panel_indices.insert(sym);
        if (symbols.isEmpty())
            continue;
        QJsonArray rows;
        for (const auto& sym : symbols)
            rows.append(QJsonObject{{"symbol", sym}, {"label", QString()}});
        QJsonArray cols;
        for (const auto& c : p.value("column_order").toArray())
            if (const QString k = col.value(c.toString()); !k.isEmpty())
                cols.append(k);
        GridItem it;
        it.id = QStringLiteral("quote_list");
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const int n = from_configs.size();
        it.cell = {(n % 3) * 4, (n / 3) * 6, 4, 6, 2, 1};
        it.config = QJsonObject{{"title", title}, {"rows", rows}, {"columns", cols}};
        from_configs.append(it);
    }
    // The PANELS canvas as the user left it (else the panels themselves):
    // what of it is shown nowhere else is kept — a list with a symbol no page
    // has, and any other widget the user put there.
    const auto panels_canvas = settings.get(layout_key(QStringLiteral("panels")));
    const auto canvas = panels_canvas.is_ok() ? layout_codec::decode(panels_canvas.value()) : std::nullopt;
    QVector<GridItem> kept;
    for (const auto& it : canvas ? canvas->items : from_configs) {
        bool shown = it.id == QLatin1String("quote_list");
        for (const auto& r : it.config.value("rows").toArray())
            shown = shown && covered.contains(fx_key(r.toObject().value("symbol").toString()));
        if (!shown)
            kept.append(it);
    }
    if (!kept.isEmpty()) {
        GridLayout l;
        l.items = compact_vertical(kept);
        l.rows = std::max(12, max_row(l.items));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.set(layout_key(id), layout_codec::encode(l), "markets");
        mine.append(QJsonObject{{"id", id}, {"title", QStringLiteral("MY PANELS")}, {"template", QStringLiteral("blank")}});
    }
    write_mine(mine);

    // MARKETS ▸ INDEX CONDITIONS gains the index rows the user had in PANELS
    // (only those: a row they took off INDEX CONDITIONS stays off).
    const auto saved = settings.get(layout_key(QStringLiteral("markets")));
    auto layout = saved.is_ok() ? layout_codec::decode(saved.value()) : std::nullopt;
    if (layout && !panel_indices.isEmpty()) {
        for (auto& it : layout->items) {
            if (it.id != QLatin1String("quote_list") ||
                it.config.value("title").toString() != QLatin1String("INDEX CONDITIONS"))
                continue;
            QJsonArray rows = it.config.value("rows").toArray();
            QSet<QString> have;
            for (const auto& r : rows)
                have.insert(r.toObject().value("symbol").toString());
            for (const auto& [sym, label] : markets::index_condition_rows())
                if (panel_indices.contains(sym) && !have.contains(sym))
                    rows.append(QJsonObject{{"symbol", sym}, {"label", label}});
            it.config.insert("rows", rows);
        }
        settings.set(layout_key(QStringLiteral("markets")), layout_codec::encode(*layout), "markets");
    }

    settings.remove(layout_key(QStringLiteral("panels")));
    settings.remove(QStringLiteral("markets_panel_configs"));
    settings.remove(QStringLiteral("markets_splitter_state"));
    settings.remove(QStringLiteral("markets_overview_layout"));
}

void MarketsScreen::load_pages() {
    migrate_pages();
    for (const auto& v : read_mine()) {
        const QJsonObject o = v.toObject();
        PageDef d{o.value("id").toString(), o.value("title").toString(), o.value("template").toString(),
                  PageDef::Mine};
        if (!d.id.isEmpty() && !d.title.isEmpty() && !is_market_page(d.id) &&
            !d.id.startsWith(QLatin1String("futures:")))
            pages_.append(d);
    }
    for (const auto& t : markets::market_pages())
        pages_.append({t.id, t.title, t.id, PageDef::Market});
    for (const auto& g : services::futures::groups())
        pages_.append({futures_page_id(g), g, futures_page_id(g), PageDef::Futures});
    widgets_.fill(nullptr, pages_.size());
}

void MarketsScreen::save_mine() {
    QJsonArray arr;
    for (const auto& d : pages_)
        if (d.kind == PageDef::Mine)
            arr.append(QJsonObject{{"id", d.id}, {"title", d.title}, {"template", d.tmpl}});
    write_mine(arr);
}

int MarketsScreen::mine_count() const {
    int n = 0;
    while (n < pages_.size() && pages_[n].kind == PageDef::Mine)
        ++n;
    return n;
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

// ── The user's screens ──────────────────────────────────────────────────────

void MarketsScreen::add_mine(const QString& title, const QString& tmpl, const GridLayout* layout) {
    const int at = mine_count();
    pages_.insert(at, {QUuid::createUuid().toString(QUuid::WithoutBraces), title, tmpl, PageDef::Mine});
    widgets_.insert(at, nullptr);
    buttons_.insert(at, nullptr);
    if (current_ >= at)
        ++current_;
    if (layout)
        ensure_page(at)->load_layout(*layout);
    save_mine();
    rebuild_mine_buttons();
    show_page(at);
}

void MarketsScreen::show_add_menu() {
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("Blank screen")), &QAction::triggered, this,
            [this]() { add_mine(QStringLiteral("SCREEN %1").arg(mine_count() + 1), QStringLiteral("blank")); });
    // A copy of any page as it is now — the user's own, a market page or a
    // FUTURES page — to make a screen of one's own from.
    auto* copy = menu.addMenu(QStringLiteral("Copy of"));
    for (int i = 0; i < pages_.size(); ++i) {
        if (i == mine_count() || (i > mine_count() && pages_[i].kind != pages_[i - 1].kind))
            copy->addSeparator();
        const QString id = pages_[i].id;
        const QString text = pages_[i].kind == PageDef::Futures ? QStringLiteral("FUTURES ▸ ") + pages_[i].title
                                                                 : pages_[i].title;
        connect(copy->addAction(QString(text).replace(QLatin1Char('&'), QStringLiteral("&&"))), &QAction::triggered,
                this, [this, id]() { duplicate_screen(index_of(id)); });
    }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Reset all of MARKETS to default…")), &QAction::triggered, this,
            &MarketsScreen::reset_all);
    menu.exec(add_btn_->mapToGlobal(QPoint(0, add_btn_->height())));
}

void MarketsScreen::show_mine_menu(int index, const QPoint& global) {
    if (index < 0 || index >= pages_.size() || pages_[index].kind != PageDef::Mine)
        return;
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("Rename…")), &QAction::triggered, this, [this, index]() { rename_screen(index); });
    connect(menu.addAction(QStringLiteral("Duplicate")), &QAction::triggered, this,
            [this, index]() { duplicate_screen(index); });
    auto* left = menu.addAction(QStringLiteral("Move left"));
    left->setEnabled(index > 0);
    connect(left, &QAction::triggered, this, [this, index]() { move_screen(index, -1); });
    auto* right = menu.addAction(QStringLiteral("Move right"));
    right->setEnabled(index + 1 < mine_count());
    connect(right, &QAction::triggered, this, [this, index]() { move_screen(index, +1); });
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Delete")), &QAction::triggered, this, [this, index]() { delete_screen(index); });
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
    save_mine();
}

void MarketsScreen::duplicate_screen(int index) {
    if (index < 0)
        return;
    GridLayout layout = ensure_page(index)->layout();
    for (auto& it : layout.items)
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto& src = pages_[index];
    const QString title = src.kind == PageDef::Mine ? src.title + QStringLiteral(" 2")
                          : src.kind == PageDef::Futures ? QStringLiteral("MY ") + src.title + QStringLiteral(" FUTURES")
                                                         : QStringLiteral("MY ") + src.title;
    // A copy of a market / FUTURES page resets to that page's default.
    add_mine(title, src.tmpl, &layout);
}

void MarketsScreen::move_screen(int index, int delta) {
    const int j = index + delta;
    if (j < 0 || j >= mine_count() || index >= mine_count())
        return;
    pages_.swapItemsAt(index, j);
    widgets_.swapItemsAt(index, j);
    buttons_.swapItemsAt(index, j);
    if (current_ == index)
        current_ = j;
    else if (current_ == j)
        current_ = index;
    save_mine();
    rebuild_mine_buttons();
}

void MarketsScreen::delete_screen(int index) {
    if (QMessageBox::question(this, QStringLiteral("Delete screen"),
                              QStringLiteral("Delete the screen “%1” and its widgets?").arg(pages_[index].title)) !=
        QMessageBox::Yes)
        return;
    const QString showing = current_ >= 0 && current_ != index ? pages_[current_].id : QString();
    auto* page = ensure_page(index);
    page->forget();
    stack_->removeWidget(page);
    page->deleteLater();
    pages_.removeAt(index);
    widgets_.removeAt(index);
    buttons_.removeAt(index);
    current_ = -1;
    save_mine();
    rebuild_mine_buttons();
    // The page being read stays; the deleted one gives way to the screen to
    // its left, else the first market page.
    const int keep = index_of(showing);
    show_page(keep >= 0 ? keep : index > 0 ? index - 1 : mine_count());
}

void MarketsScreen::reset_all() {
    if (QMessageBox::question(this, QStringLiteral("Reset MARKETS"),
                              QStringLiteral("Put every MARKETS page back to its default — MY VIEW (screens you added "
                                             "are removed), the market pages and each FUTURES page?")) !=
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
    write_mine(QJsonArray{new_my_view()});
    load_pages();
    buttons_.fill(nullptr, pages_.size());
    rebuild_mine_buttons();
    // The market and FUTURES buttons keep their places: re-point them.
    for (int i = mine_count(); i < pages_.size(); ++i)
        for (auto* b : header_bar_->findChildren<QPushButton*>())
            if (b->property("page").toString() == pages_[i].id)
                buttons_[i] = b;
    current_ = -1;
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
// Header: one row of pages, three groups
// ---------------------------------------------------------------------------

QPushButton* MarketsScreen::make_page_button(int index) {
    // "&" is a mnemonic marker in a button's text: "RATES & MACRO" needs "&&".
    auto* b = new QPushButton(QString(pages_[index].title).replace(QLatin1Char('&'), QStringLiteral("&&")));
    b->setObjectName(QStringLiteral("mkPage"));
    b->setCheckable(true);
    b->setFixedHeight(22);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("page", pages_[index].id);
    for (const auto& t : markets::market_pages())
        if (t.id == pages_[index].id)
            b->setToolTip(t.tip);
    if (pages_[index].kind == PageDef::Mine)
        b->setToolTip(QStringLiteral("Your screen — right-click to rename, duplicate, move or delete it"));
    if (pages_[index].kind == PageDef::Futures)
        b->setToolTip(QStringLiteral("%1 futures: board, chart, forward curve, contract months, spread, positioning, "
                                     "ticket — click a market and the page follows")
                          .arg(pages_[index].title));
    const QString id = pages_[index].id;
    connect(b, &QPushButton::clicked, this, [this, id]() { show_page(index_of(id)); });
    if (pages_[index].kind == PageDef::Mine) {
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this,
                [this, id, b](const QPoint& pos) { show_mine_menu(index_of(id), b->mapToGlobal(pos)); });
    }
    return b;
}

void MarketsScreen::rebuild_mine_buttons() {
    while (auto* item = mine_layout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < mine_count(); ++i) {
        buttons_[i] = make_page_button(i);
        mine_layout_->addWidget(buttons_[i]);
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
    // Groups are set apart by a rule, not named: the pages say what they are.
    auto rule = [&]() {
        h->addSpacing(10);
        auto* f = new QFrame;
        f->setObjectName(QStringLiteral("mkRule"));
        f->setFixedSize(1, 18);
        h->addWidget(f);
        h->addSpacing(10);
    };
    h->addWidget(label(QStringLiteral("FINTERM MARKETS")));
    h->addSpacing(14);

    buttons_.fill(nullptr, pages_.size());
    mine_box_ = new QWidget;
    mine_layout_ = new QHBoxLayout(mine_box_);
    mine_layout_->setContentsMargins(0, 0, 0, 0);
    mine_layout_->setSpacing(2);
    h->addWidget(mine_box_);
    add_btn_ = new QToolButton;
    add_btn_->setText(QStringLiteral("+"));
    add_btn_->setFixedSize(24, 22); // a tab's height: it reads as the group's last tab
    add_btn_->setToolTip(QStringLiteral("Add a screen of your own — blank or a copy of any page — or reset all of "
                                        "MARKETS. Right-click one of your screens to rename, duplicate, move or delete "
                                        "it."));
    add_btn_->setCursor(Qt::PointingHandCursor);
    connect(add_btn_, &QToolButton::clicked, this, &MarketsScreen::show_add_menu);
    h->addWidget(add_btn_);
    rule();

    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].kind == PageDef::Market) {
            buttons_[i] = make_page_button(i);
            h->addWidget(buttons_[i]);
        }
    rule();

    h->addWidget(label(QStringLiteral("FUTURES"), false));
    h->addSpacing(6);
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].kind == PageDef::Futures) {
            buttons_[i] = make_page_button(i);
            h->addWidget(buttons_[i]);
        }
    for (int i = 0; i < mine_count(); ++i) {
        buttons_[i] = make_page_button(i);
        mine_layout_->addWidget(buttons_[i]);
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
    reset_btn_->setToolTip("Put this page back to its default arrangement (all of MARKETS: the + menu)");
    connect(reset_btn_, &QPushButton::clicked, this, [this]() {
        if (current_ < 0)
            return;
        if (QMessageBox::question(this, QStringLiteral("Reset page"),
                                  QStringLiteral("Reset “%1” to its default arrangement?").arg(pages_[current_].title)) ==
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
                "QToolButton{background:transparent;color:%6;border:1px solid %3;font-weight:700;padding:0;}"
                "QToolButton:hover{border-color:%6;}"
                "QFrame#mkRule{background:%3;}"
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
