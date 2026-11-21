#include "screens/dashboard/widgets/MacroOddsWidget.h"

#include "datahub/DataHub.h"
#include "screens/dashboard/widgets/ConditionAddDialog.h"
#include "services/markets/ConditionService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHeaderView>
#include <QJsonArray>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finterm::screens::widgets {

using services::ConditionService;

namespace {

const QVector<ConditionRowDef>& defaults() {
    static const QVector<ConditionRowDef> k = {
        {"KXFEDDECISION", "Fed decision"},
        {"KXRECSSNBER", "US recession"},
        {"KXCPIYOY", "CPI YoY"},
    };
    return k;
}

/// How many of a series' soonest events to show.
int events_shown(const QString& series) {
    return series == QLatin1String("KXFEDDECISION") ? 2 : 1;
}

struct Outcome {
    QString label;
    double p = std::numeric_limits<double>::quiet_NaN();
};

QString pct(double p) {
    return std::isfinite(p) ? QString::number(p * 100.0, 'f', 0) + QStringLiteral("%") : ui::formatting::placeholder();
}

/// An "Above X" ladder → the two strikes either side of 50%; otherwise the
/// outcomes by probability, at most `max`.
QVector<Outcome> pick(const QJsonArray& arr, int max) {
    QVector<Outcome> all;
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        Outcome x;
        x.label = o.value("label").toString();
        if (o.value("p").isDouble())
            x.p = o.value("p").toDouble();
        all.append(x);
    }
    const bool ladder =
        all.size() > 2 && std::all_of(all.cbegin(), all.cend(), [](const Outcome& o) {
            return o.label.startsWith(QLatin1String("Above")) || o.label.startsWith(QLatin1String("Below"));
        });
    if (ladder) {
        // The strikes run in order; P(above) falls as the strike rises.
        int cross = -1;
        for (int i = 0; i + 1 < all.size(); ++i)
            if (std::isfinite(all[i].p) && std::isfinite(all[i + 1].p) && all[i].p >= 0.5 && all[i + 1].p < 0.5) {
                cross = i;
                break;
            }
        if (cross >= 0)
            return {all[cross], all[cross + 1]};
    }
    std::stable_sort(all.begin(), all.end(), [](const Outcome& a, const Outcome& b) {
        const double pa = std::isfinite(a.p) ? a.p : -1, pb = std::isfinite(b.p) ? b.p : -1;
        return pa > pb;
    });
    return all.mid(0, max);
}

} // namespace

MacroOddsWidget::MacroOddsWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget(QStringLiteral("MACRO ODDS · KALSHI"), parent, ui::colors::AMBER()) {
    ConditionService::instance().ensure_registered_with_hub();
    table_ = new ui::DataTable;
    table_->set_headers({"EVENT / OUTCOME", "PROB", ""});
    auto* h = table_->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::ResizeToContents);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    if (auto* it = table_->horizontalHeaderItem(1))
        it->setToolTip(QStringLiteral("Implied probability: the YES book's mid, else the last trade."));
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    content_layout()->addWidget(table_, 1);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row < 0 || row >= lines_.size())
            return;
        const auto& l = lines_[row];
        if (l.kind == Line::Add) {
            add_row();
        } else if (l.kind == Line::Header && col == 2 && l.series >= 0 && l.series < rows_.size()) {
            QJsonObject cfg = config();
            QJsonArray arr = cfg.value("rows").toArray();
            arr.removeAt(l.series);
            cfg["rows"] = arr;
            apply_config(cfg);
            emit config_changed(config());
        } else if (!l.url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(l.url));
        }
    });
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        QStringList topics;
        for (const auto& r : rows_)
            topics << ConditionService::kalshi_topic(r.symbol);
        datahub::DataHub::instance().request(topics, true);
    });
    apply_config(cfg);
}

QJsonObject MacroOddsWidget::config() const {
    QJsonArray arr;
    for (const auto& r : rows_)
        arr.append(QJsonObject{{"symbol", r.symbol}, {"label", r.label}});
    return QJsonObject{{"rows", arr}};
}

void MacroOddsWidget::apply_config(const QJsonObject& cfg) {
    QVector<ConditionRowDef> rows;
    if (cfg.contains(QLatin1String("rows"))) {
        for (const auto& v : cfg.value(QLatin1String("rows")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString s = o.value("symbol").toString().trimmed().toUpper();
            if (!s.isEmpty())
                rows.append({s, o.value("label").toString()});
        }
    } else {
        rows = defaults();
    }
    const bool was = subscribed_;
    if (was)
        unsubscribe_all();
    rows_ = rows;
    if (was)
        subscribe_all();
    render_.request();
}

void MacroOddsWidget::add_row() {
    ConditionAddDialog dlg(ConditionAddDialog::Kind::Kalshi, this);
    if (dlg.exec() != QDialog::Accepted || dlg.symbol().isEmpty())
        return;
    for (const auto& r : rows_)
        if (r.symbol == dlg.symbol())
            return;
    QJsonObject cfg = config();
    QJsonArray arr = cfg.value("rows").toArray();
    arr.append(QJsonObject{{"symbol", dlg.symbol()}, {"label", dlg.label()}});
    cfg["rows"] = arr;
    apply_config(cfg);
    emit config_changed(config());
}

void MacroOddsWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!subscribed_)
        subscribe_all();
}

void MacroOddsWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (subscribed_)
        unsubscribe_all();
}

void MacroOddsWidget::on_theme_changed() {
    render_.request();
}

void MacroOddsWidget::subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    QPointer<MacroOddsWidget> self = this;
    for (const auto& r : rows_) {
        const QString t = ConditionService::kalshi_topic(r.symbol);
        if (!data_.contains(t)) {
            const QJsonObject held = ConditionService::instance().last_known(t);
            if (!held.isEmpty())
                data_.insert(t, held);
        }
        hub.subscribe(this, t, [self, t](const QVariant& v) {
            if (!self || !v.canConvert<QJsonObject>())
                return;
            self->data_.insert(t, v.toJsonObject());
            self->fresh_.insert(t);
            self->error_.remove(t);
            self->render_.request();
        });
        hub.subscribe_errors(this, t, [self, t](const QString& err) {
            if (!self)
                return;
            self->error_.insert(t, err);
            self->render_.request();
        });
    }
    subscribed_ = true;
    render_.request();
}

void MacroOddsWidget::unsubscribe_all() {
    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    for (const auto& r : rows_)
        hub.unsubscribe_errors(this, ConditionService::kalshi_topic(r.symbol));
    subscribed_ = false;
}

void MacroOddsWidget::render() {
    QVector<QStringList> cells;
    lines_.clear();
    struct Style {
        QString color;
        QString tip;
        bool bold = false;
    };
    QVector<Style> styles;
    for (int si = 0; si < rows_.size(); ++si) {
        const auto& r = rows_[si];
        const QString t = ConditionService::kalshi_topic(r.symbol);
        const QJsonObject d = data_.value(t);
        const bool fresh = fresh_.contains(t);
        const QString base = QStringLiteral("https://kalshi.com/markets/") + r.symbol.toLower();
        cells.append({(r.label.isEmpty() ? r.symbol : r.label).toUpper(), QString(), QStringLiteral("−")});
        QString htip = QStringLiteral("Kalshi series %1").arg(r.symbol);
        if (d.contains("t"))
            // EVENT-STAMP: when the odds were fetched — a real instant, local time.
            htip += QStringLiteral("\nFetched %1%2")
                        .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(d.value("t").toDouble()))
                                 .toString(QStringLiteral("d MMM HH:mm")),
                             fresh ? QString() : QStringLiteral(" — updating…"));
        if (error_.contains(t))
            htip += QStringLiteral("\nLast refresh failed: %1").arg(error_.value(t));
        styles.append({ui::colors::AMBER(), htip, true});
        lines_.append({Line::Header, si, base});
        const QJsonArray events = d.value("events").toArray();
        if (events.isEmpty()) {
            cells.append({error_.contains(t) ? QStringLiteral("  unavailable — %1").arg(error_.value(t))
                                             : QStringLiteral("  loading…"),
                          QString(), QString()});
            styles.append({ui::colors::TEXT_TERTIARY(), {}, false});
            lines_.append({Line::Note, si, {}});
            continue;
        }
        for (int ei = 0; ei < std::min<qsizetype>(events.size(), events_shown(r.symbol)); ++ei) {
            const QJsonObject e = events[ei].toObject();
            const QDateTime close = QDateTime::fromString(e.value("close").toString(), Qt::ISODate);
            cells.append({QStringLiteral("  ") + e.value("title").toString(), QString(), QString()});
            styles.append({ui::colors::TEXT_SECONDARY(),
                           close.isValid() ? QStringLiteral("Closes %1").arg(close.toLocalTime().toString(
                                                 QStringLiteral("ddd d MMM yyyy HH:mm")))
                                           : QString(),
                           false});
            lines_.append({Line::Note, si, base});
            for (const auto& o : pick(e.value("outcomes").toArray(), 3)) {
                cells.append({QStringLiteral("      ") + o.label, pct(o.p), QString()});
                styles.append({fresh ? ui::colors::WHITE() : ui::colors::TEXT_SECONDARY(),
                               QStringLiteral("%1 — %2\nClick to open on kalshi.com").arg(o.label, pct(o.p)), false});
                lines_.append({Line::Outcome, si, base});
            }
        }
    }
    cells.append({QStringLiteral("+  ADD"), QString(), QString()});
    styles.append({ui::colors::AMBER(), QStringLiteral("Add a Kalshi series"), false});
    lines_.append({Line::Add, -1, {}});

    table_->set_rows(cells);
    for (int i = 0; i < cells.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            auto* it = table_->item(i, c);
            if (!it)
                continue;
            const bool remove = lines_[i].kind == Line::Header && c == 2;
            it->setForeground(QColor(remove ? ui::colors::TEXT_TERTIARY() : styles[i].color));
            it->setToolTip(remove ? QStringLiteral("Remove this series") : styles[i].tip);
            QFont f = it->font();
            f.setBold(styles[i].bold);
            it->setFont(f);
        }
    }
}

} // namespace finterm::screens::widgets
