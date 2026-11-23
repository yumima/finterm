#include "screens/dashboard/widgets/CorrelationWidget.h"

#include "datahub/DataHub.h"
#include "screens/dashboard/widgets/ConditionAddDialog.h"
#include "screens/markets/MarketChartDialog.h"
#include "services/markets/ConditionService.h"
#include "services/markets/MarketConditions.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace finterm::screens::widgets {

namespace cnd = services::conditions;
using services::ConditionService;

class CorrelationMatrix : public QWidget {
  public:
    explicit CorrelationMatrix(QWidget* parent) : QWidget(parent) {
        setMouseTracking(true);
        setMinimumHeight(120);
    }
    QStringList syms, labels;
    QVector<QVector<double>> r; ///< r[i][j]
    QSet<int> dim;              ///< rows/cols whose bars are from an earlier session
    int window = 60;
    QString message;

  protected:
    QRectF cell(int i, int j) const {
        const int n = syms.size();
        const qreal left = label_w(), top = 18;
        const qreal w = (width() - left - 2) / std::max(1, n), h = (height() - top - 2) / std::max(1, n);
        return QRectF(left + j * w, top + i * h, w, h);
    }
    qreal label_w() const { return std::min<qreal>(90, width() * 0.22); }

    static QColor heat(double v) {
        if (!std::isfinite(v))
            return QColor(ui::colors::BG_RAISED());
        const QColor pos(220, 70, 60), neg(50, 120, 220), mid(ui::colors::BG_RAISED());
        const double a = std::min(1.0, std::abs(v));
        const QColor& to = v >= 0 ? pos : neg;
        return QColor::fromRgbF(mid.redF() + (to.redF() - mid.redF()) * a, mid.greenF() + (to.greenF() - mid.greenF()) * a,
                                mid.blueF() + (to.blueF() - mid.blueF()) * a);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int n = syms.size();
        // Text never taller than its row: with many symbols in a short tile
        // the numbers give way to colour alone (the tooltip still has them).
        const qreal row_h = n > 0 ? cell(0, 0).height() : 20;
        QFont f = font();
        f.setPixelSize(std::clamp(static_cast<int>(row_h) - 3, 7, 11));
        p.setFont(f);
        const bool numbers = row_h >= 11 && cell(0, 0).width() >= 26;
        if (n == 0 || !message.isEmpty()) {
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(rect(), Qt::AlignCenter, message.isEmpty() ? QStringLiteral("Add symbols below") : message);
            return;
        }
        for (int j = 0; j < n; ++j) {
            const QRectF c = cell(0, j);
            p.setPen(QColor(dim.contains(j) ? ui::colors::TEXT_TERTIARY() : ui::colors::TEXT_SECONDARY()));
            p.drawText(QRectF(c.left(), 0, c.width(), 16), Qt::AlignCenter,
                       p.fontMetrics().elidedText(labels[j], Qt::ElideRight, static_cast<int>(c.width()) - 2));
        }
        for (int i = 0; i < n; ++i) {
            const QRectF c0 = cell(i, 0);
            p.setPen(QColor(dim.contains(i) ? ui::colors::TEXT_TERTIARY() : ui::colors::TEXT_SECONDARY()));
            p.drawText(QRectF(2, c0.top(), label_w() - 6, c0.height()), Qt::AlignRight | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(labels[i], Qt::ElideRight, static_cast<int>(label_w()) - 6));
            for (int j = 0; j < n; ++j) {
                const QRectF c = cell(i, j).adjusted(1, 1, -1, -1);
                const double v = r.value(i).value(j, cnd::kNaN);
                p.fillRect(c, i == j ? QColor(ui::colors::BG_SURFACE()) : heat(v));
                if (i == j)
                    continue;
                if (!numbers)
                    continue;
                p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
                p.drawText(c, Qt::AlignCenter, std::isfinite(v) ? QString::number(v, 'f', 2) : QString(QChar(0x2014)));
            }
        }
    }

    std::pair<int, int> hit(const QPoint& pt) const {
        for (int i = 0; i < syms.size(); ++i)
            for (int j = 0; j < syms.size(); ++j)
                if (cell(i, j).contains(pt))
                    return {i, j};
        return {-1, -1};
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const auto [i, j] = hit(e->position().toPoint());
        if (i < 0 || i == j) {
            QToolTip::hideText();
            setCursor(Qt::ArrowCursor);
            return;
        }
        setCursor(Qt::PointingHandCursor);
        const double v = r.value(i).value(j, cnd::kNaN);
        QToolTip::showText(e->globalPosition().toPoint(),
                           QStringLiteral("<b>%1 vs %2</b><br>%3-day correlation of daily changes: %4<br>"
                                          "<i>Click for the rolling correlation</i>")
                               .arg(labels[i].toHtmlEscaped(), labels[j].toHtmlEscaped())
                               .arg(window)
                               .arg(std::isfinite(v) ? QString::number(v, 'f', 3)
                                                     : QStringLiteral("— (too few common days yet)")),
                           this);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        const auto [i, j] = hit(e->position().toPoint());
        if (e->button() != Qt::LeftButton || i < 0 || i == j)
            return;
        MarketChartDialog::show_for(QStringLiteral("CORR:%1|%2|%3").arg(syms[i], syms[j]).arg(window),
                                    QStringLiteral("%1 vs %2 · %3-day correlation").arg(labels[i], labels[j]).arg(window),
                                    this);
    }
};

CorrelationWidget::CorrelationWidget(const QString& title, QVector<ConditionRowDef> defaults, const QJsonObject& cfg,
                                     QWidget* parent)
    : BaseWidget(title, parent, ui::colors::CYAN()), defaults_(std::move(defaults)) {
    ConditionService::instance().ensure_registered_with_hub();
    window_box_ = new QComboBox;
    for (int w : {30, 63, 90, 126, 252})
        window_box_->addItem(QStringLiteral("%1D").arg(w), w);
    window_box_->setToolTip(QStringLiteral("Rolling window, in trading days common to both series"));
    add_title_bar_control(window_box_);
    connect(window_box_, &QComboBox::currentIndexChanged, this, [this](int) {
        const int w = window_box_->currentData().toInt();
        if (w == window_)
            return;
        window_ = w;
        render_.request();
        emit config_changed(config());
    });
    matrix_ = new CorrelationMatrix(this);
    content_layout()->addWidget(matrix_, 1);
    // The chips scroll sideways: a dozen of them in one row would otherwise
    // set the tile's minimum width (it pinned a whole Markets column open).
    auto* strip = new QScrollArea(this);
    strip->setWidgetResizable(true);
    strip->setFrameShape(QFrame::NoFrame);
    strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    strip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    strip->setFixedHeight(34);
    strip->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    chips_ = new QWidget;
    chips_->setStyleSheet(QStringLiteral("background:transparent;"));
    chips_layout_ = new QHBoxLayout(chips_);
    chips_layout_->setContentsMargins(4, 2, 4, 2);
    chips_layout_->setSpacing(4);
    strip->setWidget(chips_);
    content_layout()->addWidget(strip);
    connect(this, &BaseWidget::refresh_requested, this, [this]() {
        QStringList topics;
        for (const auto& r : rows_)
            topics << ConditionService::stats_topic(r.symbol);
        ConditionService::instance().mark_forced(topics);
        datahub::DataHub::instance().request(topics, true);
    });
    apply_config(cfg);
}

QString CorrelationWidget::label_of(const ConditionRowDef& r) const {
    return r.label.isEmpty() ? r.symbol : r.label;
}

QJsonObject CorrelationWidget::config() const {
    QJsonArray arr;
    for (const auto& r : rows_)
        arr.append(QJsonObject{{"symbol", r.symbol}, {"label", r.label}});
    return QJsonObject{{"rows", arr}, {"window", window_}};
}

void CorrelationWidget::apply_config(const QJsonObject& cfg) {
    QVector<ConditionRowDef> rows;
    if (cfg.contains(QLatin1String("rows"))) {
        for (const auto& v : cfg.value(QLatin1String("rows")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString s = o.value("symbol").toString().trimmed();
            // FRED / derived keys have no daily closes to correlate.
            if (!s.isEmpty() && !ConditionService::is_derived(s))
                rows.append({s, o.value("label").toString()});
        }
    } else {
        rows = defaults_;
    }
    int w = cfg.value(QLatin1String("window")).toInt(63);
    if (w == 60) // the windows before 63 / 126 (three / six months of sessions)
        w = 63;
    else if (w == 120)
        w = 126;
    window_ = (w == 30 || w == 63 || w == 90 || w == 126 || w == 252) ? w : 63; // 63 = 3 months of sessions
    {
        const QSignalBlocker b(window_box_);
        window_box_->setCurrentIndex(window_box_->findData(window_));
    }
    const bool was = subscribed_;
    if (was)
        unsubscribe_all();
    rows_ = rows;
    if (was)
        subscribe_all();
    rebuild_chips();
    render_.request();
}

void CorrelationWidget::rebuild_chips() {
    while (auto* item = chips_layout_->takeAt(0)) {
        // Later, not now: the chip clicked to get here is still inside its
        // own mouse-release handler.
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    const QString chip_ss =
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;padding:1px 6px;font-size:11px;}"
                "QPushButton:hover{color:%3;border-color:%3;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::NEGATIVE());
    for (int i = 0; i < rows_.size(); ++i) {
        auto* b = new QPushButton(label_of(rows_[i]) + QStringLiteral("  ×"));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(chip_ss);
        b->setToolTip(QStringLiteral("Remove %1 (%2) from the matrix").arg(label_of(rows_[i]), rows_[i].symbol));
        const QString sym = rows_[i].symbol;
        connect(b, &QPushButton::clicked, this, [this, sym]() {
            QJsonObject cfg = config();
            QJsonArray arr;
            for (const auto& r : rows_)
                if (r.symbol != sym)
                    arr.append(QJsonObject{{"symbol", r.symbol}, {"label", r.label}});
            cfg["rows"] = arr;
            apply_config(cfg);
            emit config_changed(config());
        });
        chips_layout_->addWidget(b);
    }
    auto* add = new QPushButton(QStringLiteral("+  ADD"));
    add->setCursor(Qt::PointingHandCursor);
    add->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:1px solid %1;padding:1px 6px;"
                               "font-size:11px;font-weight:700;}")
                           .arg(ui::colors::AMBER()));
    connect(add, &QPushButton::clicked, this, &CorrelationWidget::add_row);
    chips_layout_->addWidget(add);
    chips_layout_->addStretch(1);
}

void CorrelationWidget::add_row() {
    ConditionAddDialog dlg(ConditionAddDialog::Kind::Symbol, this);
    if (dlg.exec() != QDialog::Accepted || dlg.symbol().isEmpty() || ConditionService::is_derived(dlg.symbol()))
        return;
    for (const auto& r : rows_)
        if (r.symbol.compare(dlg.symbol(), Qt::CaseInsensitive) == 0)
            return;
    QJsonObject cfg = config();
    QJsonArray arr = cfg.value("rows").toArray();
    // A short label keeps the matrix readable; the search's long name is not.
    arr.append(QJsonObject{{"symbol", dlg.symbol()}, {"label", QString()}});
    cfg["rows"] = arr;
    apply_config(cfg);
    emit config_changed(config());
}

void CorrelationWidget::showEvent(QShowEvent* e) {
    BaseWidget::showEvent(e);
    if (!subscribed_)
        subscribe_all();
}

void CorrelationWidget::hideEvent(QHideEvent* e) {
    BaseWidget::hideEvent(e);
    if (subscribed_)
        unsubscribe_all();
}

void CorrelationWidget::on_theme_changed() {
    rebuild_chips();
    matrix_->update();
}

void CorrelationWidget::subscribe_all() {
    auto& hub = datahub::DataHub::instance();
    QPointer<CorrelationWidget> self = this;
    for (const auto& r : rows_) {
        const QString sym = r.symbol;
        if (!stats_.contains(sym)) {
            const QJsonObject held = ConditionService::instance().last_known(ConditionService::stats_topic(sym));
            if (!held.isEmpty())
                stats_.insert(sym, held);
        }
        hub.subscribe(this, ConditionService::stats_topic(sym), [self, sym](const QVariant& v) {
            if (!self || !v.canConvert<QJsonObject>())
                return;
            self->stats_.insert(sym, v.toJsonObject());
            self->fresh_.insert(sym);
            self->render_.request();
        });
    }
    subscribed_ = true;
    render_.request();
}

void CorrelationWidget::unsubscribe_all() {
    datahub::DataHub::instance().unsubscribe(this);
    subscribed_ = false;
}

void CorrelationWidget::render() {
    auto closes = [this](const QString& sym) {
        QVector<cnd::DatedValue> out;
        for (const auto& v : stats_.value(sym).value("closes").toArray()) {
            const QJsonArray a = v.toArray();
            const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
            if (d.isValid() && a.at(1).isDouble())
                out.append({d, a.at(1).toDouble()});
        }
        return out;
    };
    const int n = rows_.size();
    QVector<QVector<cnd::DatedValue>> cl(n);
    matrix_->syms.clear();
    matrix_->labels.clear();
    matrix_->dim.clear();
    for (int i = 0; i < n; ++i) {
        cl[i] = closes(rows_[i].symbol);
        matrix_->syms << rows_[i].symbol;
        matrix_->labels << label_of(rows_[i]);
        if (!fresh_.contains(rows_[i].symbol))
            matrix_->dim.insert(i);
    }
    matrix_->r.assign(n, QVector<double>(n, cnd::kNaN));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const double v = cnd::correlation(cl[i], cnd::is_yield_symbol(rows_[i].symbol), cl[j],
                                              cnd::is_yield_symbol(rows_[j].symbol), window_);
            matrix_->r[i][j] = matrix_->r[j][i] = v;
        }
    matrix_->window = window_;
    matrix_->message.clear();
    bool any = false;
    for (const auto& c : cl)
        any = any || !c.isEmpty();
    if (n > 0 && !any)
        matrix_->message = QStringLiteral("Loading daily history…");
    matrix_->update();
}

} // namespace finterm::screens::widgets
