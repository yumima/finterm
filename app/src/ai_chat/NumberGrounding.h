#pragma once
// NumberGrounding.h — a model may quote a figure, never originate one.
//
// Every number in a reply is checked against the EVIDENCE: the text the model
// was actually given for this request (system prompt, history, the user's
// message, every tool result). A figure survives when
//
//   1. it is in the evidence, allowing only for how it is written — rounding
//      to the digits stated, thousands separators, K/M/B/T and "million"
//      scales, percent vs fraction; or
//   2. it is arithmetic on figures stated IN THE SAME SENTENCE that are
//      themselves grounded ("from 120 to 150, up 25%"): difference, sum,
//      ratio, percent change, percent of, product. The operands must be shown
//      right there, so the derivation is checked rather than trusted — with a
//      whole tool result to draw on, almost any number is the ratio of two
//      values somewhere.
//
// Anything else is replaced by kMarker. Nothing here judges whether a grounded
// figure is attached to the right label — that is not decidable from text. The
// guarantee is narrower and exact: no figure reaches the user that the model
// was not handed or did not visibly compute from what it was handed.
//
// Not figures (never checked): source code (a fence tagged with a programming
// language, an inline span that is an expression), list markers, identifiers
// (Q3, 10-K, S-1, 13F), index and rule names with their own number (S&P 500,
// Rule 144), bare integers 0–10 ("two reasons" written as a digit), and bare
// past or current calendar years. A year in the future is a claim and is
// checked. A figure merely WRAPPED in backticks or an untagged fence is still
// a figure. A date or clock time in the evidence grounds a date in the reply,
// never a percentage or an amount.
//
// Pure functions over QString, no Qt GUI, no I/O: unit-tested in
// tests/ai_chat/test_number_grounding.cpp.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <vector>

namespace finterm::ai_chat::grounding {

inline const QString kMarker = QStringLiteral("[unverified]");

/// The instruction every grounded request carries. The check does not depend
/// on the model obeying it; it only makes a clean answer the likely one.
inline const QString kDirective = QStringLiteral(
    "FIGURES: state a number only if it appears in the material supplied to you in this request "
    "(the conversation, the data provided, tool results). Never estimate, recall from memory, "
    "forecast or invent a figure. If you compute one, state the inputs it was computed from in the "
    "same sentence. If a figure is not in the material, say it is not available. "
    "Any figure that cannot be traced to the supplied material is removed before the user sees it.");

struct Num {
    double value = 0;   ///< magnitude with the scale applied (1.2B → 1.2e9), sign dropped
    double tol = 0;     ///< half a unit of the last digit written, scaled
    int start = 0;      ///< span in the source text, incl. currency sign and unit
    int len = 0;
    bool percent = false;
    bool currency = false;
    bool scaled = false; ///< carried an explicit K/M/B/T or "million" style scale
    bool date = false;   ///< part of a date or clock time (2026-11-14, 09:30, Nov 14)
    bool exempt = false; ///< not a figure (see header)
};

namespace detail {

inline double scale_of(const QString& unit, bool* is_scale) {
    const QString u = unit.toLower();
    *is_scale = true;
    if (unit == QLatin1String("K") || u == QLatin1String("k") || u == QLatin1String("thousand"))
        return 1e3;
    if (unit == QLatin1String("M") || unit == QLatin1String("MM") || u == QLatin1String("mn") ||
        u == QLatin1String("million"))
        return 1e6;
    if (unit == QLatin1String("B") || u == QLatin1String("bn") || u == QLatin1String("billion"))
        return 1e9;
    if (unit == QLatin1String("T") || u == QLatin1String("tn") || u == QLatin1String("trillion"))
        return 1e12;
    *is_scale = false;
    return 1.0;
}

/// [start, end) ranges that are SOURCE CODE, where a number is syntax and not
/// a claim: a fence tagged with a programming language, and an inline span
/// that reads as an expression. Backticks alone do not exempt anything — a
/// model that writes `$250` or fences a table has still stated a figure.
inline QVector<QPair<int, int>> code_ranges(const QString& t) {
    QVector<QPair<int, int>> out;
    static const QRegularExpression fence(QStringLiteral("```([A-Za-z0-9_+#-]*)[^\\n]*\\n.*?(?:```|\\z)"),
                                          QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lang(
        QStringLiteral("^(?:python|py|cpp|c\\+\\+|c|h|java|javascript|js|typescript|ts|sql|bash|sh|shell|zsh|"
                       "r|rust|go|ruby|php|swift|kotlin|scala|matlab|julia|pine|pinescript|html|css|yaml|toml|"
                       "xml|dockerfile|cmake|make|lua|perl|powershell|ps1|vb|vba|excel)$"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = fence.globalMatch(t);
    while (it.hasNext()) {
        const auto m = it.next();
        if (lang.match(m.captured(1)).hasMatch())
            out.append({int(m.capturedStart()), int(m.capturedEnd())});
    }
    static const QRegularExpression span(QStringLiteral("`[^`\\n]*`"));
    static const QRegularExpression codeish(QStringLiteral("[()=_\\[\\]{};<>]|[A-Za-z]\\.[A-Za-z]"));
    auto it2 = span.globalMatch(t);
    while (it2.hasNext()) {
        const auto m = it2.next();
        if (codeish.match(m.captured(0)).hasMatch())
            out.append({int(m.capturedStart()), int(m.capturedEnd())});
    }
    return out;
}

/// [start, end) ranges that are a date or a clock time.
inline QVector<QPair<int, int>> date_ranges(const QString& t) {
    QVector<QPair<int, int>> out;
    static const QRegularExpression re(
        QStringLiteral("\\d{4}-\\d{2}-\\d{2}(?:[T ]\\d{2}:\\d{2}(?::\\d{2}(?:\\.\\d+)?)?)?"
                       "|\\b\\d{1,2}/\\d{1,2}/\\d{2,4}\\b"
                       "|\\b\\d{1,2}:\\d{2}(?::\\d{2})?\\b"
                       "|\\b(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)[a-z]*\\.? \\d{1,2}(?:st|nd|rd|th)?"
                       "(?:,? \\d{4})?"
                       "|\\b\\d{1,2}(?:st|nd|rd|th)? (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)[a-z]*"
                       "(?:,? \\d{4})?"));
    auto it = re.globalMatch(t);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append({int(m.capturedStart()), int(m.capturedEnd())});
    }
    return out;
}

inline const QRegularExpression& number_re() {
    // currency? digits (grouped or plain) decimals? exponent? unit?
    static const QRegularExpression re(QStringLiteral(
        "([$€£¥])?"
        "(\\d{1,3}(?:,\\d{3})+|\\d+)(\\.\\d+)?([eE][-+]?\\d+)?"
        "(?:(\\s?%|\\s?percent\\b|\\s?bps\\b|\\s?basis points\\b)"
        "|(\\s?(?:thousand|million|billion|trillion)\\b|(?:MM|mn|bn|tn|[KkMBT])(?![A-Za-z]))"
        "|(x)(?![A-Za-z]))?"));
    return re;
}

/// Does the bare integer `digits` at `pos` complete a proper name — "S&P 500",
/// "Russell 2000", "Rule 144", "Step 3"? An index name is matched WITH its own
/// number, so "S&P 7,500 by year end" and "Dow 52,000" are figures, and the
/// word must stand alone, so "window 30" is not "dow 30".
inline bool follows_name(const QString& text, int pos, const QString& digits) {
    static const QRegularExpression word(QStringLiteral("([A-Za-z&]+(?: [A-Za-z]+)?)[ -]$"));
    const auto m = word.match(text.left(pos).right(24));
    if (!m.hasMatch())
        return false;
    const QString w = m.captured(1).toLower();
    const QString last = w.section(u' ', -1);
    static const QStringList any_number = {
        QStringLiteral("form"),  QStringLiteral("rule"),  QStringLiteral("section"), QStringLiteral("schedule"),
        QStringLiteral("item"),  QStringLiteral("step"),  QStringLiteral("phase"),   QStringLiteral("tier"),
        QStringLiteral("basel"), QStringLiteral("regulation")};
    if (any_number.contains(last))
        return true;
    static const QStringList pairs = {
        QStringLiteral("s&p 500"),      QStringLiteral("s&p 400"),     QStringLiteral("s&p 600"),
        QStringLiteral("s&p 100"),      QStringLiteral("s&p 1500"),    QStringLiteral("russell 1000"),
        QStringLiteral("russell 2000"), QStringLiteral("russell 3000"), QStringLiteral("nasdaq 100"),
        QStringLiteral("dow 30"),       QStringLiteral("ftse 100"),    QStringLiteral("ftse 250"),
        QStringLiteral("nikkei 225"),   QStringLiteral("cac 40"),      QStringLiteral("dax 40"),
        QStringLiteral("dax 30"),       QStringLiteral("stoxx 50"),    QStringLiteral("stoxx 600"),
        QStringLiteral("csi 300"),      QStringLiteral("kospi 200"),   QStringLiteral("asx 200"),
        QStringLiteral("tsx 60"),       QStringLiteral("fortune 500"), QStringLiteral("wilshire 5000")};
    return pairs.contains(last + u' ' + digits);
}

} // namespace detail

/// Every number written in `text`. `current_year` bounds the year exemption.
/// `lenient` is for reading EVIDENCE: nothing is skipped as an identifier or as
/// code, because a figure that is anywhere in the supplied material counts as
/// supplied ("150.25USD", a value inside a fenced JSON block).
inline QVector<Num> extract(const QString& text, int current_year, bool lenient = false) {
    QVector<Num> out;
    const auto code = lenient ? QVector<QPair<int, int>>{} : detail::code_ranges(text);
    const auto dates = detail::date_ranges(text);
    auto within = [](const QVector<QPair<int, int>>& rs, int pos) {
        for (const auto& r : rs)
            if (pos >= r.first && pos < r.second)
                return true;
        return false;
    };
    auto in_code = [&](int pos) { return within(code, pos); };
    auto it = detail::number_re().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int ds = int(m.capturedStart(2)); // first digit
        const int end = int(m.capturedEnd(0));
        if (in_code(ds))
            continue;
        const QChar prev = ds > 0 ? text.at(ds - 1) : QChar(u' ');
        const QChar prev2 = ds > 1 ? text.at(ds - 2) : QChar(u' ');
        const QChar next = end < text.size() ? text.at(end) : QChar(u' ');
        // Glued to letters or part of a longer number: an identifier or the
        // tail of a token already consumed (Q3, v2, 13F, AAPL240119C00150000).
        if (prev.isDigit() || (prev == u'.' && prev2.isDigit()))
            continue;
        const bool has_unit = !m.captured(5).isEmpty() || !m.captured(6).isEmpty() || !m.captured(7).isEmpty();
        const bool plain_int = m.captured(3).isEmpty() && m.captured(4).isEmpty() && !m.captured(2).contains(u',');
        // "$900m", "$2.5bn": a lower-case scale, read as one only after a
        // currency sign ("5m" alone is as likely minutes or metres).
        double lower_scale = 1.0;
        int lower_len = 0;
        if (!has_unit && !m.captured(1).isEmpty()) {
            static const QRegularExpression ls(QStringLiteral("^(bn|mn|tn|m|b|t|k)(?![A-Za-z])"));
            const auto lm = ls.match(text.mid(end, 3));
            if (lm.hasMatch()) {
                const QString u = lm.captured(1);
                lower_scale = u == QLatin1String("k") ? 1e3 : u.startsWith(u'm') ? 1e6 : u.startsWith(u'b') ? 1e9 : 1e12;
                lower_len = int(u.size());
            }
        }
        if (!lenient) {
            if (prev.isLetter() || prev == u'_')
                continue; // Q3, v2, ma50
            if (!has_unit && plain_int && lower_len == 0) {
                // An identifier, not a quantity: 13F, 13D, 3D (one capital
                // letter and the token ends) and ordinals. Any OTHER trailing
                // letters are a unit this parser does not know — "150USD",
                // "25bp", "30pts" — and the figure is still checked.
                const QString after = text.mid(end, 4);
                static const QRegularExpression ident(QStringLiteral("^(?:[A-Z](?![A-Za-z0-9])|(?:st|nd|rd|th)\\b)"));
                if (ident.match(after).hasMatch())
                    continue;
                // Form names: 10-K, 8-K, 10-Q …
                if (next == u'-' && end + 1 < text.size() && text.at(end + 1).isUpper())
                    continue;
                if (detail::follows_name(text, ds, m.captured(2)))
                    continue; // S&P 500, Russell 2000, Rule 144 — a name, not a quantity
            }
            if (prev == u'-' && prev2.isUpper() && plain_int)
                continue; // S-1, F-3
        }

        Num n;
        n.start = int(m.capturedStart(0));
        n.len = end + lower_len - n.start;
        n.currency = !m.captured(1).isEmpty();
        n.date = within(dates, ds);
        const QString whole = m.captured(2);
        const QString frac = m.captured(3); // ".25" or empty
        QString digits = whole;
        digits.remove(u',');
        bool ok = false;
        double v = (digits + frac + m.captured(4)).toDouble(&ok);
        if (!ok)
            continue;
        double unit = std::pow(10.0, -double(frac.isEmpty() ? 0 : frac.size() - 1));
        if (!m.captured(4).isEmpty())
            unit *= std::pow(10.0, m.captured(4).mid(1).toDouble());
        bool is_scale = false;
        double sc = detail::scale_of(m.captured(6).trimmed(), &is_scale);
        if (lower_len > 0) {
            sc = lower_scale;
            is_scale = true;
        }
        if (n.date) {
            // "2026-11-13T16:00": the T is a separator, not trillions.
            sc = 1.0;
            is_scale = false;
        }
        n.scaled = is_scale;
        n.percent = m.captured(5).contains(u'%') || m.captured(5).contains(QLatin1String("percent"));
        n.value = std::fabs(v) * sc;
        n.tol = 0.5 * unit * sc * (1.0 + 1e-9) + 1e-12;

        const bool bare = m.captured(1).isEmpty() && !has_unit && frac.isEmpty() && m.captured(4).isEmpty() &&
                          !whole.contains(u',');
        if (bare) {
            // A list marker: "3." or "3)" opening a line.
            int ls = n.start;
            while (ls > 0 && (text.at(ls - 1) == u' ' || text.at(ls - 1) == u'\t'))
                --ls;
            const bool line_start = ls == 0 || text.at(ls - 1) == u'\n';
            const bool marker = line_start && (next == u'.' || next == u')') &&
                                (end + 1 >= text.size() || text.at(end + 1).isSpace());
            const bool small = v <= 10.0;
            const bool year = whole.size() == 4 && v >= 1900 && v <= double(current_year);
            n.exempt = marker || small || year;
        }
        out.append(n);
    }
    // A range shares its unit: in "5–10%" and "$2 to $3 billion" the first
    // figure is a percentage / a scaled amount too, not a bare small integer.
    static const QRegularExpression gap(QStringLiteral("^\\s?(?:-|–|—|to)\\s?$"));
    for (int i = out.size() - 2; i >= 0; --i) {
        Num& a = out[i];
        const Num& b = out[i + 1];
        const QString between = text.mid(a.start + a.len, b.start - (a.start + a.len));
        if (!gap.match(between).hasMatch())
            continue;
        if (b.percent && !a.percent) {
            a.percent = true;
            a.exempt = false;
        }
        if (b.scaled && !a.scaled && a.value < 1e3) {
            const double sc = b.value > 0 ? std::pow(10.0, 3.0 * std::floor(std::log10(b.value) / 3.0)) : 1.0;
            a.value *= sc;
            a.tol *= sc;
            a.scaled = true;
            a.exempt = false;
        }
    }
    return out;
}

/// The figures a request was given. Order-free; add text as it is gathered.
class Evidence {
  public:
    explicit Evidence(int current_year) : year_(current_year) {}

    void add(const QString& text) {
        for (const Num& n : extract(text, year_, /*lenient=*/true))
            (n.date ? dates_ : values_).push_back(n.value);
        sorted_ = false;
    }
    bool empty() const { return values_.empty() && dates_.empty(); }

    /// Is `v` (± tol) one of the quantities given?
    bool has(double v, double tol) const {
        sort();
        return in(values_, v, tol);
    }

    /// `n` as written, or the same quantity written another way.
    bool grounds(const Num& n) const {
        if (has(n.value, n.tol))
            return true;
        const bool amount = n.percent || n.currency || n.scaled;
        // The day in "2026-11-14" grounds a date in the reply, never "14%".
        if (!amount && in(dates_, n.value, n.tol))
            return true;
        if (n.percent && has(n.value / 100.0, n.tol / 100.0))
            return true; // evidence holds the fraction (0.153 for 15.3%)
        if (n.scaled) {
            // Evidence quoted "in thousands" / "in millions", as filings do:
            // 4,512 for $4.5 billion. Only where the quotient is itself a real
            // figure — otherwise any stray 3 would ground "$3 billion".
            for (double k : {1e3, 1e6})
                if (n.value / k >= 100.0 && has(n.value / k, n.tol / k))
                    return true;
        }
        return false;
    }

  private:
    static bool in(const std::vector<double>& xs, double v, double tol) {
        const auto lo = std::lower_bound(xs.begin(), xs.end(), v - tol);
        return lo != xs.end() && *lo <= v + tol;
    }
    void sort() const {
        if (!sorted_) {
            std::sort(values_.begin(), values_.end());
            std::sort(dates_.begin(), dates_.end());
            sorted_ = true;
        }
    }
    int year_;
    mutable std::vector<double> values_; ///< quantities
    mutable std::vector<double> dates_;  ///< components of dates and clock times
    mutable bool sorted_ = true;
};

namespace detail {

/// Is `n` arithmetic on two of `ops` (grounded figures of its own sentence)?
inline bool derived(const Num& n, const std::vector<double>& ops) {
    auto hit = [&](double x) {
        if (!std::isfinite(x))
            return false;
        x = std::fabs(x);
        if (std::fabs(x - n.value) <= n.tol)
            return true;
        return n.percent && std::fabs(x * 100.0 - n.value) <= n.tol;
    };
    for (size_t i = 0; i < ops.size(); ++i) {
        for (size_t j = 0; j < ops.size(); ++j) {
            if (i == j)
                continue;
            const double a = ops[i], b = ops[j];
            if (hit(a - b) || hit(a + b) || hit(a * b) || hit(a * b / 100.0))
                return true;
            if (b != 0 && (hit(a / b) || hit((a - b) / b)))
                return true;
        }
    }
    return false;
}

struct Pass {
    QString text;
    QStringList removed;
    int checked = 0;
};

/// Check one piece of prose.
inline Pass check_text(const QString& text, const Evidence& ev, int current_year) {
    Pass out;
    const QVector<Num> nums = extract(text, current_year);
    if (nums.isEmpty()) {
        out.text = text;
        return out;
    }
    // Sentence id per number. A derivation is accepted only from operands in
    // the SAME sentence (or line): the wider the scope, the more pairs there
    // are, and the more likely an invented figure equals some pair by chance.
    QVector<int> breaks;
    {
        static const QRegularExpression stop(QStringLiteral("[.!?;](?=\\s)|\\n"));
        auto bi = stop.globalMatch(text);
        while (bi.hasNext())
            breaks.append(int(bi.next().capturedStart()));
    }
    auto para_of = [&](int pos) { return int(std::lower_bound(breaks.begin(), breaks.end(), pos) - breaks.begin()); };
    QVector<int> para(nums.size());
    QVector<bool> ok(nums.size(), false);
    for (int i = 0; i < nums.size(); ++i) {
        para[i] = para_of(nums[i].start);
        ok[i] = nums[i].exempt || ev.grounds(nums[i]);
        if (!nums[i].exempt)
            ++out.checked;
    }
    for (int i = 0; i < nums.size(); ++i) {
        if (ok[i])
            continue;
        // Operands must be real grounded figures. An exempt one — a list
        // marker, "5 reasons", a year — was never verified, and letting it in
        // would turn any round number into "derived".
        std::vector<double> ops;
        for (int j = 0; j < nums.size() && ops.size() < 8; ++j)
            if (j != i && para[j] == para[i] && !nums[j].exempt && ev.grounds(nums[j]))
                ops.push_back(nums[j].value);
        ok[i] = derived(nums[i], ops);
    }
    QString t = text;
    for (int i = nums.size() - 1; i >= 0; --i) {
        if (ok[i])
            continue;
        out.removed.prepend(text.mid(nums[i].start, nums[i].len).trimmed());
        t.replace(nums[i].start, nums[i].len, kMarker);
    }
    out.text = t;
    return out;
}

/// Check a JSON value in place: strings as prose, ungrounded numbers → null.
inline QJsonValue check_json(const QJsonValue& v, const Evidence& ev, int current_year, Pass& acc) {
    if (v.isString()) {
        Pass p = check_text(v.toString(), ev, current_year);
        acc.removed += p.removed;
        acc.checked += p.checked;
        return p.text;
    }
    if (v.isDouble()) {
        const double d = v.toDouble();
        const QString written = QString::number(d, 'g', 15);
        const QVector<Num> ns = extract(written, current_year);
        if (ns.size() != 1 || ns[0].exempt)
            return v;
        ++acc.checked;
        if (ev.grounds(ns[0]))
            return v;
        acc.removed << written;
        return QJsonValue(QJsonValue::Null);
    }
    if (v.isArray()) {
        QJsonArray out;
        for (const auto& e : v.toArray())
            out.append(check_json(e, ev, current_year, acc));
        return out;
    }
    if (v.isObject()) {
        QJsonObject out;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            out.insert(it.key(), check_json(it.value(), ev, current_year, acc));
        return out;
    }
    return v;
}

} // namespace detail

struct Result {
    QString text;        ///< the reply with every ungrounded figure replaced
    QStringList removed; ///< those figures, as the model wrote them
    int checked = 0;     ///< figures examined (exempt ones not counted)
    bool json = false;   ///< the reply is (or wraps) JSON a parser will read
};

/// Enforce the rule on a whole reply. A reply that is — or contains — one JSON
/// object is kept parseable: its strings are checked as prose and an
/// ungrounded numeric field becomes null rather than a marker.
inline Result enforce(const QString& reply, const Evidence& ev, int current_year) {
    Result r;
    // The outermost JSON value: an object, or an array (a plan, a list of
    // steps). Whichever bracket opens first is tried first.
    const int ob = int(reply.indexOf(u'{')), ab = int(reply.indexOf(u'['));
    struct Span {
        int open, close;
    };
    QVector<Span> tries;
    const Span obj{ob, int(reply.lastIndexOf(u'}'))}, arr{ab, int(reply.lastIndexOf(u']'))};
    if (ob >= 0 && (ab < 0 || ob < ab))
        tries << obj << arr;
    else
        tries << arr << obj;
    for (const Span& sp : tries) {
        if (sp.open < 0 || sp.close <= sp.open)
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply.mid(sp.open, sp.close - sp.open + 1).toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray()))
            continue;
        detail::Pass acc;
        const QJsonValue root = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
        const QJsonValue checked = detail::check_json(root, ev, current_year, acc);
        const detail::Pass head = detail::check_text(reply.left(sp.open), ev, current_year);
        const detail::Pass tail = detail::check_text(reply.mid(sp.close + 1), ev, current_year);
        r.removed = head.removed + acc.removed + tail.removed;
        r.checked = head.checked + acc.checked + tail.checked;
        r.json = true;
        // Untouched JSON is returned byte-for-byte: re-serialising would
        // reorder keys and reformat numbers for no reason.
        QString body = reply.mid(sp.open, sp.close - sp.open + 1);
        if (!acc.removed.isEmpty())
            body = QString::fromUtf8((checked.isObject() ? QJsonDocument(checked.toObject())
                                                         : QJsonDocument(checked.toArray()))
                                         .toJson(QJsonDocument::Compact));
        r.text = head.text + body + tail.text;
        return r;
    }
    const detail::Pass p = detail::check_text(reply, ev, current_year);
    r.text = p.text;
    r.removed = p.removed;
    r.checked = p.checked;
    return r;
}

/// The line appended to a prose reply that lost figures.
inline QString footnote(int removed) {
    return QStringLiteral("\n\n_%1 %2 removed: not in the data this answer was given._")
        .arg(removed)
        .arg(removed == 1 ? QStringLiteral("figure was") : QStringLiteral("figures were"));
}

} // namespace finterm::ai_chat::grounding
