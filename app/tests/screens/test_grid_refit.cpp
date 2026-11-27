// tests/screens/test_grid_refit.cpp
//
// A dashboard canvas narrower than 1000 px drops from 12 to 9 columns. The
// old path clamped each tile's x/w and compacted vertically — two tiles side
// by side in 12 columns (x=0 w=5 | x=5 w=7) became x=0 w=5 | x=2 w=7, drawn
// on top of each other, and vertical compaction never separates tiles that
// share rows. refit_to_cols must leave no two tiles overlapping.

#include "screens/dashboard/canvas/GridLayout.h"
#include "screens/dashboard/canvas/LayoutCodec.h"

#include <QtTest/QtTest>

#include <algorithm>

using namespace finterm::screens;

namespace {
GridItem item(const char* id, int x, int y, int w, int h) {
    GridItem it;
    it.id = id;
    it.instance_id = id;
    it.cell = {x, y, w, h, 1, 1};
    return it;
}
bool any_overlap(const QVector<GridItem>& items) {
    for (int i = 0; i < items.size(); ++i)
        for (int j = i + 1; j < items.size(); ++j)
            if (cells_overlap(items[i].cell, items[j].cell))
                return true;
    return false;
}
} // namespace

class TestGridRefit : public QObject {
    Q_OBJECT
  private slots:
    void side_by_side_tiles_do_not_overlap_in_fewer_columns() {
        // The Private Market default: index + upcoming, three lists, rounds.
        const QVector<GridItem> l{item("c50", 0, 0, 5, 4),     item("up", 5, 0, 7, 4),  item("g", 0, 4, 4, 4),
                                  item("l", 4, 4, 4, 4),       item("v", 8, 4, 4, 4),   item("r", 0, 8, 12, 3)};
        for (int cols : {9, 6}) {
            const auto out = refit_to_cols(l, cols);
            QCOMPARE(out.size(), l.size());
            QVERIFY2(!any_overlap(out), qPrintable(QString("overlap at %1 cols").arg(cols)));
            for (const auto& it : out)
                QVERIFY(it.cell.x >= 0 && it.cell.x + it.cell.w <= cols);
        }
    }
    void a_layout_that_fits_is_left_alone() {
        const QVector<GridItem> l{item("a", 0, 0, 6, 3), item("b", 6, 0, 6, 3)};
        const auto out = refit_to_cols(l, 12);
        QCOMPARE(out[0].cell, l[0].cell);
        QCOMPARE(out[1].cell, l[1].cell);
    }

    // A MARKETS page fits the view in a fixed number of rows. Growing a tile
    // used to add rows and shrink every tile on the page; now the tile it
    // pushes past the last row gives up height and nothing else changes.
    void growing_a_tile_keeps_the_page_rows() {
        // fx (rows 3-5) grows by one: idx (rows 5-8) is pushed to 6-9.
        QVector<GridItem> l{item("chart", 3, 0, 6, 5), item("fx", 0, 3, 3, 3), item("idx", 0, 6, 3, 3),
                            item("vol", 9, 0, 3, 2)};
        const auto out = keep_within_rows(l, "fx", 8);
        QVERIFY(out.has_value());
        for (const auto& it : *out) {
            QVERIFY(it.cell.y + it.cell.h <= 8);
            if (it.instance_id == QLatin1String("idx"))
                QCOMPARE(it.cell.h, 2); // gave up the row
            if (it.instance_id == QLatin1String("chart"))
                QCOMPARE(it.cell.h, 5); // untouched
        }
    }
    void a_resize_stops_when_the_page_is_full() {
        // idx would be left one row (below its floor of two): no.
        QVERIFY(!keep_within_rows({item("fx", 0, 3, 3, 4), item("idx", 0, 7, 3, 3)}, "fx", 8).has_value());
        // The growing tile itself past the last row: no.
        QVERIFY(!keep_within_rows({item("fx", 0, 3, 3, 6)}, "fx", 8).has_value());
        // A page with no fixed rows (the dashboard) grows as before.
        QVERIFY(keep_within_rows({item("fx", 0, 3, 3, 9)}, "fx", 0).has_value());
    }

    // The saved form carries the page's rows (v2); a v1 blob still loads,
    // its rows then taken from its tiles.
    void layout_codec_keeps_rows() {
        GridLayout l;
        l.rows = 9;
        l.items = {item("a", 0, 0, 3, 4)};
        l.items[0].config = QJsonObject{{"symbol", "^VVIX"}};
        const auto back = layout_codec::decode(layout_codec::encode(l));
        QVERIFY(back.has_value());
        QCOMPARE(back->rows, 9);
        QCOMPARE(back->items.size(), 1);
        QCOMPARE(back->items[0].cell.h, 4);
        QCOMPARE(back->items[0].config.value("symbol").toString(), QStringLiteral("^VVIX"));

        QByteArray buf; // a v1 blob, as the build before wrote it
        QDataStream st(&buf, QIODevice::WriteOnly);
        st << layout_codec::kMagic << qint32(1) << 12 << 60 << 4 << 1;
        st << QStringLiteral("a") << QStringLiteral("a") << 0 << 0 << 3 << 4 << 1 << 1 << QJsonObject();
        const auto v1 = layout_codec::decode(QString::fromLatin1(buf.toBase64()));
        QVERIFY(v1.has_value());
        QCOMPARE(v1->rows, 0);
        QCOMPARE(v1->items.size(), 1);
        QCOMPARE(v1->items[0].cell.w, 3);
    }

    void fill_down_takes_every_column_to_the_bottom() {
        // The old EQUITY default: columns ending at rows 7, 9 and 3.
        QVector<GridItem> l{item("cond_equity", 0, 0, 3, 4), item("chart", 3, 0, 6, 5), item("movers", 9, 0, 3, 3),
                            item("heatmap", 0, 4, 3, 3), item("corr", 3, 5, 6, 4)};
        QVERIFY(fill_down(l, 9));
        auto cell = [&l](const char* id) {
            return std::find_if(l.begin(), l.end(), [id](const GridItem& i) { return i.id == id; })->cell;
        };
        QCOMPARE(cell("heatmap").y + cell("heatmap").h, 9); // short column grown
        QCOMPARE(cell("movers").h, 9);                      // the right column, from row 0
        QCOMPARE(cell("cond_equity").h, 4);                 // stopped by the tile below it
        QCOMPARE(cell("chart").h, 5);
        QVERIFY(!any_overlap(l));
        QVERIFY(!fill_down(l, 9)); // filled: nothing more to do
    }

    void fill_down_lets_the_lower_narrow_tiles_have_their_room() {
        // A wide tile above two narrow ones, one shorter: the short one grows,
        // the wide one does not swallow the gap.
        QVector<GridItem> l{item("wide", 0, 0, 6, 2), item("a", 0, 2, 3, 4), item("b", 3, 2, 3, 2)};
        fill_down(l, 6);
        QCOMPARE(l[2].cell.h, 4);
        QCOMPARE(l[0].cell.h, 2);
        QVERIFY(!any_overlap(l));
    }
};

QTEST_MAIN(TestGridRefit)
#include "test_grid_refit.moc"
