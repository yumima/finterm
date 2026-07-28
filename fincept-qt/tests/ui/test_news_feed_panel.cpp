// Regression suite for the news feed's two-pane layout.
//
// The feed used to fan articles across two QListViews via parity proxies
// (even source rows left, odd rows right), with the detail pane inserted
// between them on click. It is now a single headline list on the left and a
// permanently-docked reading pane on the right. These tests lock in the
// invariants that refactor could plausibly break:
//
//   - the splitter really is two panes, and the detail pane is visible with
//     no article selected (the whole point of "stays on all the time")
//   - keyboard navigation walks rows 0,1,2,3… in order. Under the parity
//     split this had to hop between two views and map through proxies; a
//     regression here would show up as skipped or duplicated articles.
//   - clicking row N emits article N — with the proxies gone there is no
//     index mapping left, so an off-by-one would map clicks to the wrong story
//   - mark_visible_seen reports the on-screen ids exactly once

#include "screens/news/NewsFeedPanel.h"
#include "screens/news/NewsFeedModel.h"
#include "screens/news/NewsDetailPanel.h"

#include <QSignalSpy>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTest>

using namespace fincept;
using namespace fincept::screens;

// Presentation helpers the model and delegate call while building row text.
// Defined here rather than by linking NewsService.cpp, which would drag in
// LlmService, HttpClient, PythonRunner, CacheManager and PortfolioRepository
// — i.e. the whole application — for five pure formatting functions that
// have no bearing on the layout and index-mapping behaviour under test. The
// declarations still come from the real NewsService.h, so a signature change
// upstream breaks this build rather than silently diverging.
namespace fincept::services {
QString priority_color(Priority) { return QStringLiteral("#ffffff"); }
QString relative_time(int64_t) { return QStringLiteral("1m"); }
QString threat_level_string(ThreatLevel) { return QString(); }
QString threat_level_color(ThreatLevel) { return QStringLiteral("#ffffff"); }
QString NewsService::source_flag_label(SourceFlag) { return QString(); }
} // namespace fincept::services

namespace {

QVector<services::NewsArticle> make_articles(int n) {
    QVector<services::NewsArticle> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        services::NewsArticle a;
        a.id = QStringLiteral("id-%1").arg(i);
        a.headline = QStringLiteral("Headline %1").arg(i);
        a.source = QStringLiteral("SRC");
        a.time = QStringLiteral("12:00");
        a.sort_ts = 1'700'000'000 + i;
        out.push_back(a);
    }
    return out;
}

QSplitter* feed_splitter(NewsFeedPanel* panel) {
    return panel->findChild<QSplitter*>(QStringLiteral("newsFeedSplitter"));
}

} // namespace

class TestNewsFeedPanel : public QObject {
    Q_OBJECT

  private slots:
    void splitter_has_two_panes_with_detail_visible();
    void divider_is_grabbable_and_drags_both_ways();
    void detail_pane_stays_visible_across_reloads();
    void keyboard_navigation_walks_rows_in_order();
    void click_maps_to_the_row_that_was_clicked();
    void mark_visible_seen_reports_onscreen_ids();
    void brief_splits_into_summary_and_categories();
};

// The TL;DR/DIGEST output is one blob that has to land in two places: the
// summary up top, the per-category breakdown in the lower pane. Getting the
// split wrong either loses the breakdown or leaks the raw marker into the
// text the user reads.
void TestNewsFeedPanel::brief_splits_into_summary_and_categories() {
    const QString marker = NewsDetailPanel::kCategoryMarker;

    // Normal case: both halves, marker consumed.
    {
        const auto [brief, detail] =
            NewsDetailPanel::split_brief("Overall read: risk-off.\n" + marker + "\n### TECH\n- Chips");
        QCOMPARE(brief, QStringLiteral("Overall read: risk-off."));
        QCOMPARE(detail, QStringLiteral("### TECH\n- Chips"));
        QVERIFY(!brief.contains(marker));
        QVERIFY(!detail.contains(marker));
    }

    // No marker — a cached brief from before this existed, or a model that
    // ignored the instruction. All of it is the summary; nothing invented.
    {
        const auto [brief, detail] = NewsDetailPanel::split_brief(QStringLiteral("Just a brief."));
        QCOMPARE(brief, QStringLiteral("Just a brief."));
        QVERIFY(detail.isEmpty());
    }

    // Streaming: a chunk boundary lands mid-marker. The dangling fragment must
    // not be rendered as brief content.
    for (int cut = 1; cut < marker.size(); ++cut) {
        const QString partial = "Overall read: mixed.\n" + marker.left(cut);
        const auto [brief, detail] = NewsDetailPanel::split_brief(partial);
        QVERIFY2(!brief.endsWith(QLatin1Char('<')),
                 qPrintable(QStringLiteral("leaked marker prefix at cut %1: '%2'").arg(cut).arg(brief)));
        QCOMPARE(brief, QStringLiteral("Overall read: mixed."));
        QVERIFY(detail.isEmpty());
    }

    // A '<' that is NOT a marker prefix is ordinary text and must survive.
    {
        const auto [brief, detail] =
            NewsDetailPanel::split_brief(QStringLiteral("Yields fell <2% on the week"));
        QCOMPARE(brief, QStringLiteral("Yields fell <2% on the week"));
        QVERIFY(detail.isEmpty());
    }

    // Marker present but nothing after it yet (tail hasn't streamed in).
    {
        const auto [brief, detail] = NewsDetailPanel::split_brief("Brief text.\n" + marker);
        QCOMPARE(brief, QStringLiteral("Brief text."));
        QVERIFY(detail.isEmpty());
    }
}

// The layout contract: exactly two panes, detail on the right, and it is
// shown before any article has ever been selected.
void TestNewsFeedPanel::splitter_has_two_panes_with_detail_visible() {
    NewsFeedPanel panel;
    auto* detail = new QWidget;
    panel.set_detail_widget(detail);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto* split = feed_splitter(&panel);
    QVERIFY(split);
    QCOMPARE(split->count(), 2);
    QCOMPARE(split->widget(0), panel.list_view());
    QCOMPARE(split->widget(1), detail);

    // Never selected an article — the pane must still be on screen.
    QVERIFY(detail->isVisible());

    // Both panes got real width; neither collapsed to a sliver.
    const QList<int> sizes = split->sizes();
    QCOMPARE(sizes.size(), 2);
    QVERIFY(sizes[0] > 100);
    QVERIFY(sizes[1] > 100);
}

// The divider must be draggable. A QSplitter hit-tests its handle at exactly
// handleWidth px, so a 1px handle (what this shipped with) is invisible and
// unclickable even though the splitter is "resizable" in principle. Assert a
// real grab target, and that moving it redistributes width in both
// directions without either pane collapsing.
void TestNewsFeedPanel::divider_is_grabbable_and_drags_both_ways() {
    NewsFeedPanel panel;
    auto* detail = new QWidget;
    panel.set_detail_widget(detail);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto* split = feed_splitter(&panel);
    QVERIFY(split);
    QVERIFY2(split->handleWidth() >= 4,
             "divider grab area is too narrow for a mouse to hit");
    QVERIFY(!split->childrenCollapsible()); // neither pane can be dragged away

    auto* handle = split->handle(1);
    QVERIFY(handle);
    QCOMPARE(handle->width(), split->handleWidth());
    QCOMPARE(handle->cursor().shape(), Qt::SplitHCursor); // drag affordance

    const QList<int> start = split->sizes();
    const QPoint grab(handle->width() / 2, handle->height() / 2);

    // Drive the real drag path — press on the handle, move, release — rather
    // than calling setSizes(), so this exercises what a user's mouse does.
    auto drag_by = [&](int dx) {
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, grab);
        QTest::mouseMove(handle, grab + QPoint(dx, 0));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, grab + QPoint(dx, 0));
    };

    // Drag left: headline list shrinks, reading pane grows, total preserved.
    drag_by(-150);
    const QList<int> left = split->sizes();
    QVERIFY2(left[0] < start[0], "dragging left did not shrink the headline pane");
    QVERIFY(left[1] > start[1]);
    QCOMPARE(left[0] + left[1], start[0] + start[1]);

    // And back the other way from the new position.
    drag_by(150);
    const QList<int> right = split->sizes();
    QVERIFY2(right[0] > left[0], "dragging right did not grow the headline pane");
    QVERIFY(right[1] < left[1]);
    QVERIFY(right[1] > 0); // childrenCollapsible(false) keeps it on screen
    QCOMPARE(right[0] + right[1], start[0] + start[1]);
}

// A feed refresh swaps the model's contents. The reading pane is not part of
// that data flow and must not blink out.
void TestNewsFeedPanel::detail_pane_stays_visible_across_reloads() {
    NewsFeedPanel panel;
    auto* detail = new QWidget;
    panel.set_detail_widget(detail);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    panel.model()->set_wire_articles(make_articles(20));
    QVERIFY(detail->isVisible());
    panel.model()->set_wire_articles({});
    QVERIFY(detail->isVisible());
    panel.model()->set_wire_articles(make_articles(5));
    QVERIFY(detail->isVisible());
}

// select_next/select_previous must sweep the model in row order.
void TestNewsFeedPanel::keyboard_navigation_walks_rows_in_order() {
    NewsFeedPanel panel;
    panel.set_detail_widget(new QWidget);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.model()->set_wire_articles(make_articles(6));

    QSignalSpy spy(&panel, &NewsFeedPanel::article_clicked);

    // First select_next with no current row lands on row 0, then advances.
    for (int i = 0; i < 4; ++i)
        panel.select_next();
    QCOMPARE(spy.count(), 4);
    for (int i = 0; i < 4; ++i) {
        const auto a = spy.at(i).at(0).value<services::NewsArticle>();
        QCOMPARE(a.id, QStringLiteral("id-%1").arg(i));
    }
    QCOMPARE(panel.current_article().id, QStringLiteral("id-3"));

    spy.clear();
    panel.select_previous();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<services::NewsArticle>().id, QStringLiteral("id-2"));
    QCOMPARE(panel.current_article().id, QStringLiteral("id-2"));
}

// Row N must emit article N — no proxy mapping is left to get wrong, and
// this is the assertion that would catch it if one crept back in.
void TestNewsFeedPanel::click_maps_to_the_row_that_was_clicked() {
    NewsFeedPanel panel;
    panel.set_detail_widget(new QWidget);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.model()->set_wire_articles(make_articles(8));

    QSignalSpy spy(&panel, &NewsFeedPanel::article_clicked);
    auto* view = panel.list_view();

    // Emit clicked() directly rather than synthesising mouse events: row
    // geometry comes from a custom delegate, so a coordinate-based click
    // would be testing the delegate's metrics, not the index mapping we
    // care about here.
    for (int row : {0, 3, 7, 1}) {
        const QModelIndex idx = panel.model()->index(row, 0);
        QVERIFY(idx.isValid());
        emit view->clicked(idx);
    }

    QCOMPARE(spy.count(), 4);
    const QList<int> expected{0, 3, 7, 1};
    for (int i = 0; i < expected.size(); ++i) {
        const auto a = spy.at(i).at(0).value<services::NewsArticle>();
        QCOMPARE(a.id, QStringLiteral("id-%1").arg(expected[i]));
    }
}

// Seen-tracking walks the one list now instead of two proxies. Every id it
// reports must be a real article, and it must not report duplicates.
void TestNewsFeedPanel::mark_visible_seen_reports_onscreen_ids() {
    NewsFeedPanel panel;
    panel.set_detail_widget(new QWidget);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.model()->set_wire_articles(make_articles(40));
    QCOMPARE(panel.model()->rowCount(), 40);

    QSet<QString> seen;
    panel.mark_visible_seen(seen);

    QVERIFY(!seen.isEmpty());              // something was on screen
    QVERIFY(seen.size() <= 40);            // never more ids than articles
    for (const auto& id : seen)
        QVERIFY(id.startsWith(QStringLiteral("id-")));

    // Idempotent: a second walk over an unscrolled viewport adds nothing new.
    const int first = seen.size();
    panel.mark_visible_seen(seen);
    QCOMPARE(seen.size(), first);

    // Everything reported was marked seen in the model, so the unseen count
    // dropped by exactly that many.
    QCOMPARE(panel.model()->unseen_count(), 40 - first);
}

QTEST_MAIN(TestNewsFeedPanel)
#include "test_news_feed_panel.moc"
