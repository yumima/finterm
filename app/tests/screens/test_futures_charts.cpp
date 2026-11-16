// The Futures screen's charts painted on synthetic data: a curve with a month
// that did not trade (a gap, never a guess), stale months, the implied-rate
// view, and a positioning history with holes.
//
// Set FINTERM_RENDER_DIR to a directory to keep the pictures.
#include "screens/futures/FuturesCharts.h"

#include <QDir>
#include <QtTest>

#include <cmath>

using namespace finterm::screens;
namespace fut = finterm::services::futures;

namespace {
void keep(const QImage& img, const QString& name) {
    const QString dir = qEnvironmentVariable("FINTERM_RENDER_DIR");
    if (!dir.isEmpty())
        QVERIFY(img.save(QDir(dir).filePath(name)));
}

/// Pixels that are neither background nor near it: something was drawn.
int inked(const QImage& img) {
    const QRgb bg = img.pixel(1, 1);
    int n = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2)
            n += img.pixel(x, y) != bg;
    return n;
}
} // namespace

class TestFuturesCharts : public QObject {
    Q_OBJECT
  private slots:
    void charts_render() {
        fut::Curve curve;
        curve.root = "CL";
        curve.front = "CLZ26.NYM";
        QVector<fut::PastCurve> past(2);
        past[0] = {"1W", 5, QDate(2026, 11, 9), {}};
        past[1] = {"1M", 21, QDate(2026, 10, 16), {}};
        for (int i = 0; i < 24; ++i) {
            fut::CurveContract c;
            c.year = 2026 + (11 + i) / 12;
            c.month = (11 + i) % 12 + 1;
            c.symbol = QStringLiteral("CL%1%2.NYM").arg(QLatin1Char("FGHJKMNQUVXZ"[c.month - 1])).arg(c.year % 100);
            c.price = 87.0 - i * 0.55;
            c.prev_close = c.price + 1.4;
            c.stale = i > 20;
            curve.contracts.append(c);
            if (i != 7) // a month that did not trade that session: a gap, not a guess
                past[0].closes.insert(c.symbol, c.price - 2.0);
            past[1].closes.insert(c.symbol, c.price + 3.0 - i * 0.1);
        }
        futures::CurveChart chart;
        chart.resize(760, 300);
        chart.set_curve(curve, past, false, 2);
        const QImage drawn = chart.grab().toImage();
        keep(drawn, QStringLiteral("futures_curve.png"));
        QVERIFY(inked(drawn) > 2000);
        chart.set_curve(curve, {}, true, 4); // as an implied rate
        chart.set_message(QStringLiteral("The curve did not load"));
        QVERIFY(!chart.grab().isNull());

        fut::CotReport rep;
        for (int i = 0; i < 156; ++i) {
            fut::CotRow r;
            r.date = QDate(2023, 11, 21).addDays(7 * i);
            r.oi = 1.8e6;
            r.nc_long = 300000 + 60000 * std::sin(i / 11.0);
            r.nc_short = 180000;
            r.c_long = 800000;
            r.c_short = 950000 + 50000 * std::sin(i / 11.0);
            if (i % 40 != 0) { // the small traders' line has holes
                r.nr_long = 70000;
                r.nr_short = 45000;
            }
            rep.rows.append(r);
        }
        fut::SpreadHistory sh;
        for (int i = 0; i < 120; ++i)
            sh.points.append({QDate(2026, 5, 18).addDays(i), 85.0, 85.0 - 2.0 + std::sin(i / 15.0), -2.0 + std::sin(i / 15.0) * 2.5});
        futures::SpreadChart spread;
        spread.resize(520, 220);
        spread.set_history(sh, 2);
        const QImage spread_img = spread.grab().toImage();
        keep(spread_img, QStringLiteral("futures_spread.png"));
        QVERIFY(inked(spread_img) > 1000);
        spread.set_message(QStringLiteral("no second month"));
        QVERIFY(!spread.grab().isNull());

        futures::CotChart cot;
        cot.resize(620, 240);
        cot.set_report(rep);
        const QImage positions = cot.grab().toImage();
        keep(positions, QStringLiteral("futures_cot.png"));
        QVERIFY(inked(positions) > 2000);
        cot.set_report({});
        QVERIFY(!cot.grab().isNull());
    }
};

QTEST_MAIN(TestFuturesCharts)
#include "test_futures_charts.moc"
