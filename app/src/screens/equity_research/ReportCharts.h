// src/screens/equity_research/ReportCharts.h
#pragma once
// Charts for the ER Report, painted to images so the same report can be shown
// on screen (theme colours), printed to PDF and written to a self-contained
// HTML file (paper colours). QPainter only.
//
// Every chart takes NaN as "no value": a missing bar is a gap, a missing
// point breaks the line. Nothing is interpolated or drawn as zero.

#include "screens/equity_research/ReportModel.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace finterm::screens::report {

struct Palette {
    QColor bg, panel, border, text, text_dim, grid, accent, positive, negative;
    QVector<QColor> series;
    QString font_family;
    int font_px = 12;

    static Palette from_theme();
    /// Dark ink on white, for PDF and HTML export.
    static Palette paper();
    QString hex(const QColor& c) const { return c.name(); }
};

using Format = std::function<QString(double)>;

struct Series {
    QString name;
    QVector<double> values; ///< one per category; NaN = none
    int color = 0;          ///< index into Palette::series
};

struct ChartSpec {
    QString title;
    QString note; ///< one line under the title: unit, source
    QStringList categories;
    QVector<Series> bars;
    bool stacked = false;
    QVector<Series> lines;
    /// Lines are read off their own axis on the right (a margin over revenue bars).
    bool lines_on_right = false;
    Format fmt;       ///< bar / left axis values
    Format fmt_right; ///< right axis values
    /// Print each bar's value above it (few categories only).
    bool bar_labels = false;
    /// Start the left axis at the data's own range instead of zero (a price line).
    bool free_baseline = false;
    /// Draw a rule at this left-axis value (NaN = none), e.g. 100 on a rebased series.
    double rule = qQNaN();
};

QImage render_chart(const ChartSpec& spec, const Palette& pal, QSize logical, qreal dpr);

/// Horizontal bars, one per label; one of them highlighted (the company).
struct HBarSpec {
    QString title;
    QString note;
    QStringList labels;
    QVector<double> values;
    int highlight = -1;
    Format fmt;
    double reference = qQNaN(); ///< a marked value (the peer median)
    QString reference_label;
};
QImage render_hbars(const HBarSpec& spec, const Palette& pal, QSize logical, qreal dpr);

/// A low–high range with a marked mean and the current price.
struct RangeSpec {
    QString title;
    QString note;
    double low = qQNaN(), mean = qQNaN(), high = qQNaN(), current = qQNaN();
    QString mean_label, current_label;
    Format fmt;
};
QImage render_range(const RangeSpec& spec, const Palette& pal, QSize logical, qreal dpr);

/// Rows of parts that sum to a whole (analyst ratings by month).
struct StackSpec {
    QString title;
    QString note;
    QStringList part_names;
    QVector<QColor> part_colors;
    QStringList row_labels;
    QVector<QVector<double>> rows;
};
QImage render_stack(const StackSpec& spec, const Palette& pal, QSize logical, qreal dpr);

} // namespace finterm::screens::report
