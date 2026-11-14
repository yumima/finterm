#pragma once
// MacroReleases.h — the printed number for a US macro release that is out.
//
// The calendar feed (ForexFactory) carries the consensus ("forecast") and the
// pre-release prior, but never the actual. The actual comes from FRED's
// public, key-less series download (fred.stlouisfed.org/graph/fredgraph.csv)
// and is derived here with the same transform the headline uses (m/m %, y/y %,
// change in thousands, level).
//
// The trap is staleness: right after 08:30 ET FRED often still ends at LAST
// month's observation, and reading "the latest value" would print last
// month's number as today's actual. So every series states its release lag,
// and an actual is shown only when FRED's latest observation is the period
// THIS release reports (expected_period). Otherwise the actual is absent —
// the row says "awaiting FRED", never a stale number and never 0.
//
// Header-only, Qt-Core-only, pure; tests/services/test_macro_releases.cpp.

#include <QDate>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <optional>

namespace finterm::macro_releases {

enum class Transform {
    PctMoM,   ///< (x_t / x_{t-1} - 1) · 100, "0.3%"
    PctYoY,   ///< vs 12 observations earlier (monthly), "2.9%"
    DiffK,    ///< x_t - x_{t-1}, series already in thousands, "119K"
    LevelPct, ///< the value is a percent, "4.4%"
    LevelK,   ///< persons → thousands, "227K"
    LevelMFromK, ///< series in thousands → millions, "7.67M"
};

enum class Frequency { Monthly, Quarterly, Weekly };

struct SeriesSpec {
    QString fred_id;
    Transform transform = Transform::PctMoM;
    Frequency freq = Frequency::Monthly;
    int lag_months = 1; ///< Monthly: the release in month M reports month M − lag
};

/// FRED series for a ForexFactory US release title; nullopt when the release
/// has no FRED equivalent the repo can derive faithfully (ISM — not on FRED;
/// existing home sales — NAR; UoM sentiment — FRED lags the preliminary).
inline std::optional<SeriesSpec> spec_for(const QString& ff_title) {
    const QString t = ff_title.trimmed().toLower();
    auto has = [&](const char* s) { return t.contains(QLatin1String(s)); };
    const bool mm = has("m/m"), yy = has("y/y");
    if (has("cpi") && !has("median") && !has("trimmed")) {
        if (has("core"))
            return mm ? SeriesSpec{"CPILFESL", Transform::PctMoM}
                 : yy ? std::optional<SeriesSpec>(SeriesSpec{"CPILFENS", Transform::PctYoY})
                      : std::nullopt;
        return mm ? SeriesSpec{"CPIAUCSL", Transform::PctMoM}
             : yy ? std::optional<SeriesSpec>(SeriesSpec{"CPIAUCNS", Transform::PctYoY})
                  : std::nullopt;
    }
    if (has("ppi") && mm)
        return has("core") ? SeriesSpec{"PPIFES", Transform::PctMoM} : SeriesSpec{"PPIFIS", Transform::PctMoM};
    if (has("pce price index")) {
        if (has("core"))
            return mm ? SeriesSpec{"PCEPILFE", Transform::PctMoM}
                 : yy ? std::optional<SeriesSpec>(SeriesSpec{"PCEPILFE", Transform::PctYoY})
                      : std::nullopt;
        return mm ? SeriesSpec{"PCEPI", Transform::PctMoM}
             : yy ? std::optional<SeriesSpec>(SeriesSpec{"PCEPI", Transform::PctYoY})
                  : std::nullopt;
    }
    if (has("non-farm employment change") || has("nonfarm payrolls"))
        return SeriesSpec{"PAYEMS", Transform::DiffK};
    if (t == QLatin1String("unemployment rate"))
        return SeriesSpec{"UNRATE", Transform::LevelPct};
    if (has("unemployment claims") && !has("continuing"))
        return SeriesSpec{"ICSA", Transform::LevelK, Frequency::Weekly, 0};
    if (has("average hourly earnings") && mm)
        return SeriesSpec{"CES0500000003", Transform::PctMoM};
    if (has("gdp q/q") && !has("price"))
        return SeriesSpec{"A191RL1Q225SBEA", Transform::LevelPct, Frequency::Quarterly, 0};
    if (has("retail sales") && mm)
        return has("core") ? SeriesSpec{"RSFSXMV", Transform::PctMoM} : SeriesSpec{"RSAFS", Transform::PctMoM};
    if (has("jolts job openings"))
        return SeriesSpec{"JTSJOL", Transform::LevelMFromK, Frequency::Monthly, 2};
    if (has("industrial production") && mm)
        return SeriesSpec{"INDPRO", Transform::PctMoM};
    if (t == QLatin1String("housing starts"))
        return SeriesSpec{"HOUST", Transform::LevelMFromK};
    if (t == QLatin1String("building permits"))
        return SeriesSpec{"PERMIT", Transform::LevelMFromK};
    if (has("durable goods orders") && mm)
        return has("core") ? SeriesSpec{"ADXTNO", Transform::PctMoM} : SeriesSpec{"DGORDER", Transform::PctMoM};
    return std::nullopt;
}

struct Obs {
    QDate date; ///< FRED's observation date (period start; weekly: week-ending Saturday)
    double value = NAN;
};

/// FRED's fredgraph.csv: "observation_date,SERIES" then "2026-09-01,324.5".
/// Missing values ("." or empty) are skipped — absent, not zero.
inline QVector<Obs> parse_fred_csv(const QString& csv) {
    QVector<Obs> out;
    const QStringList lines = csv.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        const int comma = line.indexOf(QLatin1Char(','));
        if (comma <= 0)
            continue;
        const QDate d = QDate::fromString(line.left(comma), Qt::ISODate);
        if (!d.isValid())
            continue; // header
        bool ok = false;
        const double v = line.mid(comma + 1).trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(v))
            continue;
        out.append({d, v});
    }
    return out;
}

/// The observation period a release on `release_day` reports: the first of
/// month M − lag (monthly), the start of the last full quarter that ended
/// before the release month (quarterly — advance, second and third estimates
/// alike), or the window of Saturdays 1–7 days before (weekly: returned as
/// the earliest acceptable Saturday; see is_expected).
inline QDate expected_period(const SeriesSpec& s, const QDate& release_day) {
    switch (s.freq) {
    case Frequency::Monthly: {
        const QDate m(release_day.year(), release_day.month(), 1);
        return m.addMonths(-s.lag_months);
    }
    case Frequency::Quarterly: {
        // The latest quarter-END month strictly before the release month.
        QDate p = QDate(release_day.year(), release_day.month(), 1).addMonths(-1);
        while (p.month() % 3 != 0)
            p = p.addMonths(-1);
        return p.addMonths(-2);
    }
    case Frequency::Weekly:
        return release_day.addDays(-7);
    }
    return {};
}

inline bool is_expected(const SeriesSpec& s, const QDate& obs, const QDate& release_day) {
    if (!obs.isValid() || !release_day.isValid())
        return false;
    if (s.freq == Frequency::Weekly)
        return obs < release_day && obs >= release_day.addDays(-7);
    return obs == expected_period(s, release_day);
}

inline QString format_value(Transform t, double v) {
    if (!std::isfinite(v))
        return {};
    switch (t) {
    case Transform::PctMoM:
    case Transform::PctYoY:
    case Transform::LevelPct: {
        QString s = QString::number(v, 'f', 1);
        if (s == QLatin1String("-0.0"))
            s = QStringLiteral("0.0");
        return s + QLatin1Char('%');
    }
    case Transform::DiffK:
        return QString::number(std::lround(v)) + QLatin1Char('K');
    case Transform::LevelK:
        return QString::number(std::lround(v / 1000.0)) + QLatin1Char('K');
    case Transform::LevelMFromK:
        return QString::number(v / 1000.0, 'f', 2) + QLatin1Char('M');
    }
    return {};
}

/// The transformed value at observation index `i`; NaN when the history is
/// too short.
inline double value_at(const QVector<Obs>& obs, int i, Transform t) {
    if (i < 0 || i >= obs.size())
        return NAN;
    switch (t) {
    case Transform::PctMoM:
        return i >= 1 && obs[i - 1].value != 0.0 ? (obs[i].value / obs[i - 1].value - 1.0) * 100.0 : NAN;
    case Transform::PctYoY: {
        // 12 observations back must be the same month a year earlier — a gap
        // in the series would otherwise compare against the wrong month.
        if (i < 12 || obs[i - 12].date != obs[i].date.addYears(-1) || obs[i - 12].value == 0.0)
            return NAN;
        return (obs[i].value / obs[i - 12].value - 1.0) * 100.0;
    }
    case Transform::DiffK:
        return i >= 1 ? obs[i].value - obs[i - 1].value : NAN;
    case Transform::LevelPct:
    case Transform::LevelK:
    case Transform::LevelMFromK:
        return obs[i].value;
    }
    return NAN;
}

struct Printed {
    QString actual;  ///< empty when FRED has not posted this release's period yet
    QString prior;   ///< FRED's current (revised) value for the period before
    QDate period;    ///< the observation the actual is for (invalid when absent)
};

/// Actual and prior for a release that came out on `release_day` (ET date).
inline Printed printed(const SeriesSpec& s, const QVector<Obs>& obs, const QDate& release_day) {
    Printed p;
    if (obs.isEmpty())
        return p;
    const int last = obs.size() - 1;
    if (is_expected(s, obs[last].date, release_day)) {
        p.actual = format_value(s.transform, value_at(obs, last, s.transform));
        p.prior = format_value(s.transform, value_at(obs, last - 1, s.transform));
        if (!p.actual.isEmpty())
            p.period = obs[last].date;
    }
    return p;
}

/// "Oct 2026", "Q3 2026", "w/e Nov 7".
inline QString period_label(const SeriesSpec& s, const QDate& d) {
    if (!d.isValid())
        return {};
    const QLocale en(QLocale::English);
    switch (s.freq) {
    case Frequency::Monthly:
        return en.toString(d, QStringLiteral("MMM yyyy"));
    case Frequency::Quarterly:
        return QStringLiteral("Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.year());
    case Frequency::Weekly:
        return QStringLiteral("w/e ") + en.toString(d, QStringLiteral("MMM d"));
    }
    return {};
}

} // namespace finterm::macro_releases
