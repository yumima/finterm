#pragma once
// MarketTemplates.h — the pages MARKETS starts with, and what is on each.
//
// MARKETS is one header row of pages, in three groups, nothing shown twice:
//   MY VIEW …   the user's own screens (+ adds one: blank or a copy of any
//               page); MY VIEW is the first, a desk-wide read the user built
//   MARKETS · EQUITY · RATES & MACRO · WORLD
//               the market pages: conditions by asset class; inside the US
//               market; the curve, credit and the economy; the global board
//   FUTURES     a page per group (MAJORS · INDEX · RATES …): board, chart,
//               forward curve, months, detail (spread · COT · ticket · terms)
// Every page has a chart linked to its own clicks and fits one view (its
// rows fit the window). The default arrangement of each page is the one the
// desk settled on.

#include "screens/dashboard/canvas/GridLayout.h"

#include <QString>
#include <QVector>

namespace finterm::screens::markets {

struct ScreenTemplate {
    QString id;    ///< the page's id (a built-in page) / the template's id
    QString title; ///< tab text
    QString tip;
};

/// The market pages, in tab order: MARKETS, EQUITY, RATES & MACRO, WORLD.
const QVector<ScreenTemplate>& market_pages();
/// MY VIEW — the screen a first open starts the user's group with.
const ScreenTemplate& my_view_template();

/// The layout a page built from `template_id` starts with: a market page's
/// id, "myview", "futures:<GROUP>" or "blank" (empty; also unknown).
GridLayout template_layout(const QString& template_id);

/// The FUTURES page a root opens on: MAJORS when it is one, else its group.
QString futures_group_for(const QString& root);

/// The index rows of MARKETS ▸ INDEX CONDITIONS (the Stock Indices list of the
/// old PANELS folded in) — also merged into a saved INDEX CONDITIONS list once.
const QVector<std::pair<QString, QString>>& index_condition_rows();

} // namespace finterm::screens::markets
