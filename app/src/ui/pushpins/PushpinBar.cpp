#include "ui/pushpins/PushpinBar.h"

#include "core/symbol/SymbolDragSource.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "services/pushpins/PushpinService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/TickerBar.h"

#include <QHBoxLayout>

#include <cmath>
#include <limits>

namespace finterm::ui {

PushpinBar::PushpinBar(QWidget* parent) : QWidget(parent) {
    setObjectName("pushpinBar");
    setFixedHeight(26);
    setAttribute(Qt::WA_StyledBackground, true);
    auto apply_theme = [this]() {
        setStyleSheet(QString("#pushpinBar{background:%1;border-bottom:1px solid %2;}")
                          .arg(colors::BG_BASE(), colors::BORDER_DIM()));
    };
    apply_theme();
    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [apply_theme](const ThemeTokens&) { apply_theme(); });

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 1, 0, 1);
    outer->setSpacing(0);
    ticker_ = new TickerBar(this);
    outer->addWidget(ticker_, 1);

    // Dropping a symbol anywhere on the banner pins it (the tape included) —
    // not a drag that started on the tape itself, and not a second pin of a
    // symbol already pinned (with more context, e.g. from the equity screen).
    const auto pin = [this](const SymbolRef& ref, SymbolGroup) {
        if (ticker_->dragging())
            return;
        auto& pins = PushpinService::instance();
        for (const SymbolRef& r : pins.pins())
            if (r.symbol.compare(ref.symbol, Qt::CaseInsensitive) == 0)
                return;
        pins.pin(ref);
    };
    symbol_dnd::installDropFilter(this, pin);
    symbol_dnd::installDropFilter(ticker_, pin);

    connect(ticker_, &TickerBar::symbols_changed, this, [this]() {
        if (active_)
            resubscribe(/*force=*/true);
    });
    connect(&PushpinService::instance(), &PushpinService::pins_changed, this, [this]() {
        if (active_)
            resubscribe(/*force=*/true);
    });
}

PushpinBar::~PushpinBar() {
    unsubscribe();
}

void PushpinBar::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    resubscribe(/*force=*/false);
}

void PushpinBar::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    unsubscribe();
}

QStringList PushpinBar::banner_symbols() const {
    QStringList out;
    for (const SymbolRef& r : PushpinService::instance().pins()) {
        const QString s = r.symbol.trimmed().toUpper();
        if (!s.isEmpty() && !out.contains(s))
            out << s;
    }
    for (const QString& s : ticker_->symbols())
        if (!out.contains(s))
            out << s;
    return out;
}

void PushpinBar::resubscribe(bool force) {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    active_ = true;
    subscribed_ = banner_symbols();
    // A pin from a crypto venue (BTCUSDT, BTC/USDT) is no Yahoo symbol: it is
    // drawn, but a quote request would only ever fail.
    no_quote_.clear();
    for (const SymbolRef& r : PushpinService::instance().pins())
        if (r.asset_class.compare(QLatin1String("crypto"), Qt::CaseInsensitive) == 0 ||
            r.symbol.contains(QLatin1Char('/')))
            no_quote_.insert(r.symbol.trimmed().toUpper());

    // Keep what is still on the banner and seed the rest from the hub's
    // last-known value, so a re-show draws the whole tape at once rather than
    // only the topics still inside their TTL.
    for (auto it = cache_.begin(); it != cache_.end();)
        it = subscribed_.contains(it.key()) ? std::next(it) : cache_.erase(it);
    for (const QString& sym : std::as_const(subscribed_)) {
        if (cache_.contains(sym))
            continue;
        const QVariant last = hub.peek_raw(QStringLiteral("market:quote:") + sym);
        if (last.canConvert<services::QuoteData>())
            cache_.insert(sym, last.value<services::QuoteData>());
    }
    render();
    if (subscribed_.isEmpty())
        return;

    QStringList topics;
    topics.reserve(subscribed_.size());
    for (const QString& sym : std::as_const(subscribed_)) {
        if (no_quote_.contains(sym))
            continue;
        const QString topic = QStringLiteral("market:quote:") + sym;
        topics.append(topic);
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            cache_.insert(sym, v.value<services::QuoteData>());
            render_.request(); // one render per tick batch
        });
    }
    // force: a changed list must not sit blank until the topics' min interval.
    if (!topics.isEmpty())
        hub.request(topics, force);
}

void PushpinBar::unsubscribe() {
    if (!active_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    active_ = false;
}

void PushpinBar::render() {
    QSet<QString> pinned;
    for (const SymbolRef& r : PushpinService::instance().pins())
        pinned.insert(r.symbol.trimmed().toUpper());
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    QVector<TickerBar::Entry> entries;
    entries.reserve(subscribed_.size());
    for (const QString& sym : std::as_const(subscribed_)) {
        const bool is_pinned = pinned.contains(sym);
        if (const auto it = cache_.constFind(sym); it != cache_.constEnd()) {
            // The tape shows the percent change.
            entries.append({sym, it->price, it->change_pct, is_pinned});
        } else if (is_pinned) {
            // A pin is shown even before (or without) a quote: the user put it there.
            entries.append({sym, kNaN, kNaN, true});
        }
    }
    ticker_->set_data(entries);
}

} // namespace finterm::ui
