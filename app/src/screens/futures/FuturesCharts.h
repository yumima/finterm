// src/screens/futures/FuturesCharts.h
#pragma once
// QPainter charts for the Futures screen: the forward curve (now against
// where the same months settled before) and the CFTC positioning history.
// Plain QWidgets on purpose — see VideoRenderWidget for why nothing here is
// OpenGL-backed.

#include "services/futures/FuturesMath.h"

#include <QWidget>

namespace finterm::screens::futures {

class CurveChart : public QWidget {
    Q_OBJECT
  public:
    explicit CurveChart(QWidget* parent = nullptr);

    /// `as_rate` plots 100 − price (Fed Funds, SOFR). `decimals` is the
    /// contract's price precision.
    void set_curve(const services::futures::Curve& curve, const QVector<services::futures::PastCurve>& past,
                   bool as_rate, int decimals);
    void set_message(const QString& message);

  signals:
    void contract_clicked(const QString& symbol);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

  private:
    struct Series {
        QString label;
        QColor color;
        Qt::PenStyle style = Qt::SolidLine;
        QVector<double> values; ///< one per contract; NaN = no point
    };
    QRectF plot_rect() const;
    double x_at(int i, const QRectF& plot) const;
    int index_at(double x) const;
    QVector<Series> build_series() const;
    double shown(double price) const;

    services::futures::Curve curve_;
    QVector<services::futures::PastCurve> past_;
    bool as_rate_ = false;
    int decimals_ = 2;
    QString message_;
    int hover_ = -1;
};

class CotChart : public QWidget {
    Q_OBJECT
  public:
    explicit CotChart(QWidget* parent = nullptr);
    void set_report(const services::futures::CotReport& report);
    void set_message(const QString& message);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

  private:
    QRectF plot_rect() const;

    services::futures::CotReport report_;
    QString message_;
    int hover_ = -1;
};

} // namespace finterm::screens::futures
