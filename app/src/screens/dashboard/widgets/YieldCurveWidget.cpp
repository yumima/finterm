#include "screens/dashboard/widgets/YieldCurveWidget.h"
#include "screens/dashboard/canvas/SelectionLink.h"

#include "datahub/DataHub.h"
#include "screens/markets/MarketChartDialog.h"
#include "services/markets/ConditionService.h"
#include "services/markets/MarketConditions.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QToolTip>

#include <cmath>

namespace finterm::screens::widgets {

namespace cnd = services::conditions;
using services::ConditionService;

namespace {

struct CurveRow {
    QDate date;
    QHash<QString, double> y;
};

QVector<CurveRow> rows_of(const QJsonObject& o) {
    QVector<CurveRow> out; // newest first, as the Treasury file is read
    for (const auto& v : o.value(QLatin1String("rows")).toArray()) {
        const QJsonObject r = v.toObject();
        CurveRow c;
        c.date = QDate::fromString(r.value("date").toString(), Qt::ISODate);
        if (!c.date.isValid())
            continue;
        for (const auto& k : YieldCurveWidget::tenors()) {
            const QJsonValue x = r.value(k);
            if (x.isDouble())
                c.y.insert(k, x.toDouble());
        }
        out.append(c);
    }
    return out;
}

/// The newest row on or before `d` (rows newest first); nullptr if none.
const CurveRow* row_on_or_before(const QVector<CurveRow>& rows, const QDate& d) {
    for (const auto& r : rows)
        if (r.date <= d)
            return &r;
    return nullptr;
}

const QStringList kDefaultSpreads = {"y10-y2", "y10-m3", "y30-y5"};

QString spread_label(const QString& key) {
    const QStringList p = key.split(QLatin1Char('-'));
    if (p.size() != 2)
        return key;
    static const QHash<QString, QString> shortn = {{"m1", "1M"}, {"m2", "2M"}, {"m3", "3M"}, {"m4", "4M"},
                                                   {"m6", "6M"}, {"y1", "1"},  {"y2", "2"},  {"y3", "3"},
                                                   {"y5", "5"},  {"y7", "7"},  {"y10", "10"}, {"y20", "20"},
                                                   {"y30", "30"}};
    const QString a = shortn.value(p[1]), b = shortn.value(p[0]);
    if (key == QLatin1String("y10-m3"))
        return QStringLiteral("3M10Y");
    if (!a.endsWith('M') && !b.endsWith('M'))
        return QStringLiteral("%1s%2s").arg(a, b);
    return QStringLiteral("%1–%2").arg(YieldCurveWidget::tenor_label(p[1]), YieldCurveWidget::tenor_label(p[0]));
}

double spread_bp(const CurveRow* r, const QString& key) {
    if (!r)
        return cnd::kNaN;
    const QStringList p = key.split(QLatin1Char('-'));
    if (p.size() != 2 || !r->y.contains(p[0]) || !r->y.contains(p[1]))
        return cnd::kNaN;
    return (r->y.value(p[0]) - r->y.value(p[1])) * 100.0;
}

QString bp_text(double v, bool sign) {
    if (!std::isfinite(v))
        return ui::formatting::placeholder();
    const QString body = QString::number(std::abs(v), 'f', 0);
    if (!sign)
        return (v < 0 ? QString(QChar(0x2212)) : QString()) + body + QStringLiteral("bp");
    if (body == QLatin1String("0"))
        return QStringLiteral("0bp");
    return (v > 0 ? QStringLiteral("+") : QString(QChar(0x2212))) + body + QStringLiteral("bp");
}

} // namespace

// ── The painted curve ───────────────────────────────────────────────────────

class YieldCurveCanvas : public QWidget {
  public:
    explicit YieldCurveCanvas(QWidget* parent) : QWidget(parent) {
        setMouseTracking(true);
        setMinimumHeight(110);
        setCursor(Qt::PointingHandCursor);
    }
    struct Line {
        QString name;
        QDate date;
        QHash<QString, double> y;
        QString color;
        bool dashed = false;
    };
    QVector<Line> lines;
    QString message;

    QString tenor_at(int x) const {
        const auto& t = YieldCurveWidget::tenors();
        const QRectF r = plot();
        if (t.size() < 2 || r.width() <= 0)
            return {};
        const int i = static_cast<int>(std::lround((x - r.left()) / r.width() * (t.size() - 1)));
        return i >= 0 && i < t.size() ? t[i] : QString();
    }

  protected:
    QRectF plot() const { return QRectF(34, 18, width() - 44, height() - 36); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& t = YieldCurveWidget::tenors();
        const QRectF r = plot();
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        if (lines.isEmpty()) {
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(rect(), Qt::AlignCenter, message.isEmpty() ? QStringLiteral("Loading the curve…") : message);
            return;
        }
        double lo = 1e9, hi = -1e9;
        for (const auto& l : lines)
            for (auto it = l.y.cbegin(); it != l.y.cend(); ++it) {
                lo = std::min(lo, it.value());
                hi = std::max(hi, it.value());
            }
        if (!(hi > lo)) {
            hi = lo + 0.5;
            lo -= 0.5;
        }
        const double pad = (hi - lo) * 0.1;
        lo -= pad;
        hi += pad;
        auto X = [&](int i) { return r.left() + r.width() * i / std::max<qsizetype>(1, t.size() - 1); };
        auto Y = [&](double v) { return r.bottom() - (v - lo) / (hi - lo) * r.height(); };
        // grid + axes
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
        for (int k = 0; k <= 4; ++k) {
            const double v = lo + (hi - lo) * k / 4;
            p.drawLine(QPointF(r.left(), Y(v)), QPointF(r.right(), Y(v)));
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(QRectF(0, Y(v) - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 2));
            p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
        }
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
        for (int i = 0; i < t.size(); ++i)
            p.drawText(QRectF(X(i) - 14, r.bottom() + 3, 28, 14), Qt::AlignCenter, YieldCurveWidget::tenor_label(t[i]));
        // lines, oldest drawn first so today's is on top
        for (int li = lines.size() - 1; li >= 0; --li) {
            const auto& l = lines[li];
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < t.size(); ++i) {
                if (!l.y.contains(t[i]))
                    continue;
                const QPointF pt(X(i), Y(l.y.value(t[i])));
                started ? path.lineTo(pt) : path.moveTo(pt);
                started = true;
            }
            QPen pen(QColor(l.color), li == 0 ? 2.0 : 1.3);
            if (l.dashed)
                pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawPath(path);
        }
        // legend
        qreal lx = r.left();
        for (const auto& l : lines) {
            p.setPen(QColor(l.color));
            const QString s = QStringLiteral("● %1 %2").arg(l.name, l.date.toString(QStringLiteral("d MMM yy")));
            p.drawText(QPointF(lx, 12), s);
            lx += p.fontMetrics().horizontalAdvance(s) + 12;
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QString k = tenor_at(e->position().toPoint().x());
        if (k.isEmpty() || lines.isEmpty()) {
            QToolTip::hideText();
            return;
        }
        QStringList tip{QStringLiteral("<b>%1 Treasury</b>").arg(YieldCurveWidget::tenor_label(k))};
        for (const auto& l : lines)
            tip << QStringLiteral("%1 (%2): %3")
                       .arg(l.name, l.date.toString(Qt::ISODate),
                            l.y.contains(k) ? QString::number(l.y.value(k), 'f', 2) + QStringLiteral("%")
                                            : ui::formatting::placeholder());
        tip << QStringLiteral("<i>Click for its chart</i>");
        QToolTip::showText(e->globalPosition().toPoint(), tip.join(QStringLiteral("<br>")), this);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        const QString k = tenor_at(e->position().toPoint().x());
        if (e->button() == Qt::LeftButton && !k.isEmpty())
            MarketChartDialog::show_for(QStringLiteral("UST:") + k,
                                        QStringLiteral("US %1 Treasury (par)").arg(YieldCurveWidget::tenor_label(k)),
                                        this);
    }
};

// ── Widget ──────────────────────────────────────────────────────────────────

const QStringList& YieldCurveWidget::tenors() {
    static const QStringList k = {"m1", "m3", "m6", "y1", "y2", "y3", "y5", "y7", "y10", "y20", "y30"};
    return k;
}

QString YieldCurveWidget::tenor_label(const QString& key) {
    if (key.startsWith('m'))
        return key.mid(1) + QStringLiteral("M");
    if (key.startsWith('y'))
        return key.mid(1) + QStringLiteral("Y");
    return key;
}

YieldCurveWidget::YieldCurveWidget(const QJsonObject& cfg, QWidget* parent)
    : BaseWidget(QStringLiteral("US YIELD CURVE"), parent, ui::colors::CYAN()) {
    ConditionService::instance().ensure_registered_with_hub();
    canvas_ = new YieldCurveCanvas(this);
    content_layout()->addWidget(canvas_, 3);
    table_ = new ui::DataTable;
    table_->set_headers({"SPREAD", "NOW", "1D", "1M", "1Y PCTL", ""});
    auto* h = table_->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::ResizeToContents);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    if (auto* it = table_->horizontalHeaderItem(4))
        it->setToolTip(QStringLiteral("Where today's spread ranks among the past year's business days (0 = lowest)."));
    table_->viewport()->setCursor(Qt::PointingHandCursor);
    content_layout()->addWidget(table_, 2);
    SelectionLink::follow_rows(table_, [this](int r) {
        return r >= 0 && r < spreads_.size() ? QStringLiteral("UST:") + spreads_[r] : QString();
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row == spreads_.size()) {
            add_spread();
            return;
        }
        if (row < 0 || row >= spreads_.size())
            return;
        if (col == 5) {
            spreads_.removeAt(row);
            render_.request();
            emit config_changed(config());
            return;
        }
        MarketChartDialog::show_for(QStringLiteral("UST:") + spreads_[row],
                                    QStringLiteral("US %1 spread (bp)").arg(spread_label(spreads_[row])), this);
    });
    connect(this, &BaseWidget::refresh_requested, this,
            [] { datahub::DataHub::instance().request(ConditionService::kUstTopic, true); });
    apply_config(cfg);
}

QJsonObject YieldCurveWidget::config() const {
    return QJsonObject{{"spreads", QJsonArray::fromStringList(spreads_)}};
}

void YieldCurveWidget::apply_config(const QJsonObject& cfg) {
    if (cfg.contains(QLatin1String("spreads"))) {
        spreads_.clear();
        for (const auto& v : cfg.value(QLatin1String("spreads")).toArray())
            if (v.toString().split('-').size() == 2)
                spreads_ << v.toString();
    } else {
        spreads_ = kDefaultSpreads;
    }
    render_.request();
}

void YieldCurveWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (subscribed_)
        return;
    if (data_.isEmpty())
        data_ = ConditionService::instance().last_known(ConditionService::kUstTopic);
    QPointer<YieldCurveWidget> self = this;
    auto& hub = datahub::DataHub::instance();
    hub.subscribe(this, ConditionService::kUstTopic, [self](const QVariant& v) {
        if (!self || !v.canConvert<QJsonObject>())
            return;
        self->data_ = v.toJsonObject();
        self->fresh_ = true;
        self->error_.clear();
        self->render_.request();
    });
    hub.subscribe_errors(this, ConditionService::kUstTopic, [self](const QString& err) {
        if (!self)
            return;
        self->error_ = err;
        self->render_.request();
    });
    subscribed_ = true;
    render_.request();
}

void YieldCurveWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (!subscribed_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    datahub::DataHub::instance().unsubscribe_errors(this, ConditionService::kUstTopic);
    subscribed_ = false;
}

void YieldCurveWidget::on_theme_changed() {
    render_.request();
}

void YieldCurveWidget::render() {
    const auto rows = rows_of(data_);
    canvas_->lines.clear();
    canvas_->message = error_.isEmpty() ? QString() : QStringLiteral("Treasury curve unavailable: %1").arg(error_);
    const CurveRow* now = rows.isEmpty() ? nullptr : &rows.first();
    const CurveRow* m1 = now ? row_on_or_before(rows, now->date.addMonths(-1)) : nullptr;
    const CurveRow* y1 = now ? row_on_or_before(rows, now->date.addYears(-1)) : nullptr;
    const CurveRow* d1 = rows.size() > 1 ? &rows[1] : nullptr;
    if (now) {
        canvas_->lines.append({QStringLiteral("Now"), now->date, now->y,
                               fresh_ ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY(), false});
        if (m1)
            canvas_->lines.append({QStringLiteral("1M ago"), m1->date, m1->y, ui::colors::CYAN(), false});
        if (y1)
            canvas_->lines.append({QStringLiteral("1Y ago"), y1->date, y1->y, ui::colors::TEXT_TERTIARY(), true});
    }
    canvas_->update();

    QVector<QStringList> cells;
    for (const auto& s : spreads_) {
        const double v = spread_bp(now, s);
        QVector<double> hist;
        for (const auto& r : rows)
            if (y1 && r.date > y1->date) {
                const double x = spread_bp(&r, s);
                if (std::isfinite(x))
                    hist.append(x);
            }
        const double pct = y1 ? cnd::percent_rank(hist, v) : cnd::kNaN;
        cells.append({spread_label(s), bp_text(v, false), bp_text(v - spread_bp(d1, s), true),
                      bp_text(v - spread_bp(m1, s), true),
                      std::isfinite(pct) ? QString::number(std::lround(pct)) : ui::formatting::placeholder(),
                      QStringLiteral("−")});
    }
    cells.append({QStringLiteral("+  ADD"), {}, {}, {}, {}, {}});
    table_->set_rows(cells);
    for (int i = 0; i < spreads_.size(); ++i) {
        const double v = spread_bp(now, spreads_[i]);
        if (auto* it = table_->item(i, 1))
            it->setForeground(QColor(v < 0 ? ui::colors::NEGATIVE() : ui::colors::WHITE()));
        for (int c = 2; c <= 3; ++c)
            if (auto* it = table_->item(i, c))
                it->setForeground(QColor(ui::colors::TEXT_SECONDARY()));
        if (auto* it = table_->item(i, 5)) {
            it->setForeground(QColor(ui::colors::TEXT_TERTIARY()));
            it->setToolTip(QStringLiteral("Remove this spread"));
        }
        if (auto* it = table_->item(i, 0))
            it->setToolTip(QStringLiteral("Long minus short par yield, in basis points. Negative = inverted.\n"
                                          "Source: US Treasury daily par yield curve, end of day%1.\nClick for its chart.")
                               .arg(now ? QStringLiteral(" (%1)").arg(now->date.toString(Qt::ISODate)) : QString()));
    }
    if (auto* it = table_->item(spreads_.size(), 0)) {
        it->setForeground(QColor(ui::colors::AMBER()));
        it->setToolTip(QStringLiteral("Add a spread between two tenors"));
    }
    if (!rows.isEmpty())
        set_title(QStringLiteral("US YIELD CURVE · %1%2")
                      .arg(now->date.toString(QStringLiteral("d MMM")), fresh_ ? QString() : QStringLiteral(" · updating")));
}

void YieldCurveWidget::add_spread() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add spread"));
    dlg.setStyleSheet(QString("QDialog{background:%1;color:%2;} QLabel{color:%2;}")
                          .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
    auto* form = new QFormLayout(&dlg);
    auto* lon = new QComboBox;
    auto* sho = new QComboBox;
    for (const auto& t : tenors()) {
        lon->addItem(tenor_label(t), t);
        sho->addItem(tenor_label(t), t);
    }
    lon->setCurrentIndex(tenors().indexOf(QStringLiteral("y10")));
    sho->setCurrentIndex(tenors().indexOf(QStringLiteral("y2")));
    form->addRow(QStringLiteral("Long tenor"), lon);
    form->addRow(QStringLiteral("minus short tenor"), sho);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || lon->currentData() == sho->currentData())
        return;
    const QString key = lon->currentData().toString() + QLatin1Char('-') + sho->currentData().toString();
    if (spreads_.contains(key))
        return;
    spreads_ << key;
    render_.request();
    emit config_changed(config());
}

} // namespace finterm::screens::widgets
