#pragma once
// MarketTemplates.h — the screens MARKETS starts with, and what is on each.
//
// MARKETS is one header row of pages, by job, nothing shown twice:
//   OVERVIEW  the user's screens — by default MARKETS (conditions by class,
//             vol, index conditions, FX, crypto, MSTR), EQUITY (sectors,
//             heatmap, sector correlation, movers), RATES & MACRO (curve,
//             10Y–2Y, credit, macro + CAPE, correlation, sovereign spreads)
//   FUTURES   a page per group (MAJORS · INDEX · RATES …): board, chart,
//             forward curve, months, detail (spread · COT · ticket · terms)
//   WORLD     the global price board as tiles
// Every page has a chart linked to its own clicks, and fits one view (a
// 12 × 12 grid whose rows fit the window).

#include "screens/dashboard/canvas/GridLayout.h"

#include <QString>
#include <QVector>

namespace finterm::screens::markets {

struct ScreenTemplate {
    QString id;    ///< also the default screen's id
    QString title; ///< tab text
    QString tip;
};

/// The default screens, in tab order — and "Reset all screens" restores them.
const QVector<ScreenTemplate>& default_screens();
/// Templates offered by "+" besides the defaults (WORLD).
const QVector<ScreenTemplate>& extra_templates();

/// The layout a page built from `template_id` starts with: an OVERVIEW
/// screen's id, "futures:<GROUP>" (a FUTURES page), "world", "panels" (the
/// user's old PANELS, one Quote List each) or "blank" (empty; also unknown).
GridLayout template_layout(const QString& template_id);

/// The FUTURES page a root opens on: MAJORS when it is one, else its group.
QString futures_group_for(const QString& root);

} // namespace finterm::screens::markets
