// tests/screens/test_dashboard_templates.cpp
//
// The built-in dashboard templates: every one fits the 12-column grid with no
// two tiles overlapping, and the first-run default (Market Desk) comes out of
// apply_template's vertical compaction exactly as it was laid out — the tiles,
// sizes and places the owner's own dashboard has.

#include "screens/dashboard/canvas/DashboardTemplates.h"

#include <QtTest/QtTest>

#include <algorithm>

using namespace finterm::screens;

namespace {
QVector<GridItem> with_ids(QVector<GridItem> items) {
    for (int i = 0; i < items.size(); ++i)
        items[i].instance_id = QString::number(i);
    return items;
}
} // namespace

class TestDashboardTemplates : public QObject {
    Q_OBJECT
  private slots:
    void every_template_fits_the_grid() {
        for (const auto& t : all_dashboard_templates()) {
            QVERIFY2(!t.items.isEmpty(), qPrintable(t.id));
            for (const auto& it : t.items) {
                QVERIFY2(it.cell.x >= 0 && it.cell.y >= 0 && it.cell.w >= 1 && it.cell.h >= 1,
                         qPrintable(t.id + ' ' + it.id));
                QVERIFY2(it.cell.x + it.cell.w <= 12, qPrintable(t.id + ' ' + it.id));
                QVERIFY2(it.cell.min_w <= it.cell.w && it.cell.min_h <= it.cell.h, qPrintable(t.id + ' ' + it.id));
            }
            for (int i = 0; i < t.items.size(); ++i)
                for (int j = i + 1; j < t.items.size(); ++j)
                    QVERIFY2(!cells_overlap(t.items[i].cell, t.items[j].cell),
                             qPrintable(t.id + ": " + t.items[i].id + " / " + t.items[j].id));
        }
    }

    void market_desk_is_kept_as_laid_out() {
        const auto all = all_dashboard_templates();
        const auto it = std::find_if(all.begin(), all.end(), [](const auto& t) { return t.id == "market_desk"; });
        QVERIFY(it != all.end());
        const auto laid = with_ids(it->items);
        const auto compacted = compact_vertical(laid);
        QCOMPARE(compacted.size(), laid.size());
        for (const auto& a : laid) {
            const auto b = std::find_if(compacted.begin(), compacted.end(),
                                        [&](const GridItem& x) { return x.instance_id == a.instance_id; });
            QVERIFY(b != compacted.end());
            QVERIFY2(b->cell == a.cell, qPrintable(a.id));
        }
    }

    void market_desk_carries_no_ones_portfolio_ids() {
        for (const auto& t : all_dashboard_templates())
            for (const auto& it : t.items)
                QVERIFY2(!it.config.contains("portfolio_ids") && !it.config.contains("portfolio_id"),
                         qPrintable(t.id + ' ' + it.id));
    }
};

QTEST_MAIN(TestDashboardTemplates)
#include "test_dashboard_templates.moc"
