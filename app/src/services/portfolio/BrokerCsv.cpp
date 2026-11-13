#include "services/portfolio/BrokerCsv.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace finterm::portfolio::csv {

namespace {

// Lower-case, letters and digits only: "Cost Basis ($)" → "costbasis".
QString norm(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        if (c.isLetterOrNumber())
            out += c.toLower();
    return out;
}

struct Synonym {
    Field field;
    const char* name;
    int rank; // lower wins when two columns claim one field
};

// Exact (normalised) header names seen in US broker exports. Order inside a
// field is preference: "Trade Date" beats "Settlement Date".
const Synonym kSynonyms[] = {
    {Field::Date, "tradedate", 0},
    {Field::Date, "rundate", 0},
    {Field::Date, "date", 1},
    {Field::Date, "transactiondate", 1},
    {Field::Date, "activitydate", 1},
    {Field::Date, "processdate", 2},
    {Field::Date, "settlementdate", 3},
    {Field::Date, "settledate", 3},

    {Field::Action, "action", 0},
    {Field::Action, "transactiontype", 0},
    {Field::Action, "transcode", 0},
    {Field::Action, "buysell", 0},
    {Field::Action, "activitytype", 1},
    {Field::Action, "type", 2},
    {Field::Action, "activity", 2},

    {Field::Symbol, "symbol", 0},
    {Field::Symbol, "ticker", 0},
    {Field::Symbol, "tickersymbol", 0},
    {Field::Symbol, "securitysymbol", 0},
    {Field::Symbol, "instrument", 1},
    {Field::Symbol, "symbolcusip", 1},

    {Field::Quantity, "quantity", 0},
    {Field::Quantity, "qty", 0},
    {Field::Quantity, "shares", 0},
    {Field::Quantity, "qtyquantity", 0},
    {Field::Quantity, "sharesheld", 0},
    {Field::Quantity, "units", 1},

    {Field::Price, "price", 0},
    {Field::Price, "shareprice", 0},
    {Field::Price, "pricepershare", 0},
    {Field::Price, "executionprice", 0},
    {Field::Price, "tradeprice", 0},
    {Field::Price, "lastprice", 1},
    {Field::Price, "currentprice", 1},
    {Field::Price, "marketprice", 2},

    {Field::Amount, "amount", 0},
    {Field::Amount, "netamount", 0},
    {Field::Amount, "netcashamount", 0},
    {Field::Amount, "totalamount", 1},
    {Field::Amount, "proceeds", 1},
    {Field::Amount, "principalamount", 2},

    {Field::CostBasisTotal, "costbasis", 0},
    {Field::CostBasisTotal, "costbasistotal", 0},
    {Field::CostBasisTotal, "totalcostbasis", 0},
    {Field::CostBasisTotal, "totalcost", 1},
    {Field::CostBasisTotal, "cost", 2},

    {Field::AverageCost, "averagecost", 0},
    {Field::AverageCost, "averagecostbasis", 0},
    {Field::AverageCost, "avgcost", 0},
    {Field::AverageCost, "costpershare", 0},
    {Field::AverageCost, "costbasispershare", 0},
    {Field::AverageCost, "unitcost", 0},
    {Field::AverageCost, "averageprice", 1},
    {Field::AverageCost, "avgprice", 1},
};

std::optional<std::pair<Field, int>> match(const QString& header) {
    const QString n = norm(header);
    if (n.isEmpty())
        return std::nullopt;
    for (const auto& s : kSynonyms)
        if (n == QLatin1String(s.name))
            return std::make_pair(s.field, s.rank);
    return std::nullopt;
}

QChar detect_delimiter(const QString& text) {
    const QStringList lines = text.left(20000).split('\n');
    int best = 0;
    QChar best_d = ',';
    for (const QChar d : {QChar(','), QChar(';'), QChar('\t')}) {
        int score = 0;
        for (int i = 0; i < std::min<qsizetype>(lines.size(), 30); ++i)
            score += lines[i].count(d);
        if (score > best) {
            best = score;
            best_d = d;
        }
    }
    return best_d;
}

// RFC 4180: quoted fields may hold the delimiter, doubled quotes, newlines.
QVector<QStringList> split_rows(const QString& text, QChar delim, QVector<int>* line_numbers) {
    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    int line = 1, row_line = 1;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                if (c == '\n')
                    ++line;
                cell += c;
            }
            continue;
        }
        if (c == '"' && cell.trimmed().isEmpty()) {
            quoted = true;
            cell.clear();
        } else if (c == delim) {
            row << cell.trimmed();
            cell.clear();
        } else if (c == '\n') {
            row << cell.trimmed();
            rows << row;
            line_numbers->append(row_line);
            row.clear();
            cell.clear();
            row_line = ++line;
        } else if (c != '\r') {
            cell += c;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row << cell.trimmed();
        rows << row;
        line_numbers->append(row_line);
    }
    return rows;
}

bool row_blank(const QStringList& row) {
    return std::all_of(row.cbegin(), row.cend(), [](const QString& c) { return c.isEmpty(); });
}

QString cell(const QStringList& row, int col) {
    return col >= 0 && col < row.size() ? row[col] : QString();
}

// Broker symbol → the Yahoo-style symbol the rest of the app uses.
QString clean_symbol(QString s) {
    s = s.trimmed().toUpper();
    while (s.endsWith('*'))
        s.chop(1);
    s.replace('/', '-'); // BRK/B
    s.replace('.', '-'); // BRK.B
    return s;
}

// Why a row's symbol is not a security we track, or empty when it is one.
QString non_security_reason(const QString& raw) {
    const QString up = raw.trimmed().toUpper();
    if (up.isEmpty() || up == QLatin1String("--"))
        return QStringLiteral("no symbol");
    if (up.contains(QLatin1String("TOTAL")))
        return QStringLiteral("a total row");
    // Only with a space: "Cash & Cash Investments", "Pending Activity". A bare
    // CASH is a real ticker (Pathward Financial).
    if (up.contains(' ') && (up.contains(QLatin1String("CASH")) || up.contains(QLatin1String("PENDING")) ||
                             up.contains(QLatin1String("MONEY MARKET"))))
        return QStringLiteral("cash / pending — not a security");
    if (up.endsWith(QLatin1String("**")))
        return QStringLiteral("money-market sweep — counted as cash, not a holding");
    // Options: "AAPL 01/17/2025 150.00 C", "-AAPL250117C150", OCC "AAPL  250117C00150000".
    static const QRegularExpression option_re(
        QStringLiteral(R"(^-?[A-Z]{1,6}\s*\d{6}[CP]\d+|\s\d{1,2}/\d{1,2}/\d{2,4}\s|\s(CALL|PUT)\b)"));
    if (option_re.match(up).hasMatch())
        return QStringLiteral("an option — options aren't supported");
    if (up.contains(' '))
        return QStringLiteral("not a ticker symbol");
    return {};
}

// BUY / SELL / DIVIDEND, or the reason a row can't be booked (prefixed "!").
std::optional<QString> classify_action(const QString& action) {
    const QString a = action.toLower();
    // Not every "short": "SHORT-TERM CAP GAIN" is a fund distribution.
    if (a.contains(QLatin1String("sell short")) || a.contains(QLatin1String("short sale")) ||
        a.contains(QLatin1String("to cover")))
        return QStringLiteral("!a short sale — short positions aren't supported");
    if (a.contains(QLatin1String("split")))
        return QStringLiteral("!a split — the ratio isn't in the row; add it in the Transactions panel");
    if (a.contains(QLatin1String("stock div")) || a.contains(QLatin1String("spinoff")) ||
        a.contains(QLatin1String("spin-off")))
        return QStringLiteral("!shares received without a price — add them in the Transactions panel");
    if (a.contains(QLatin1String("reinvest")) && a.contains(QLatin1String("div")))
        return QStringLiteral("DIVIDEND"); // the cash leg; the share leg is its own row
    if (a.contains(QLatin1String("reinvest")))
        return QStringLiteral("BUY");
    if (a.contains(QLatin1String("dividend")) || a.contains(QLatin1String("div ")) || a == QLatin1String("cdiv") ||
        a.contains(QLatin1String("qual div")))
        return QStringLiteral("DIVIDEND");
    if (a.contains(QLatin1String("buy")) || a.contains(QLatin1String("bought")) || a.contains(QLatin1String("purchase")))
        return QStringLiteral("BUY");
    if (a.contains(QLatin1String("sell")) || a.contains(QLatin1String("sold")) || a.contains(QLatin1String("sale")))
        return QStringLiteral("SELL");
    return std::nullopt;
}

} // namespace

QString field_name(Field f) {
    switch (f) {
        case Field::Ignore:
            return QStringLiteral("(ignore)");
        case Field::Date:
            return QStringLiteral("Date");
        case Field::Action:
            return QStringLiteral("Action");
        case Field::Symbol:
            return QStringLiteral("Symbol");
        case Field::Quantity:
            return QStringLiteral("Quantity");
        case Field::Price:
            return QStringLiteral("Price");
        case Field::Amount:
            return QStringLiteral("Amount");
        case Field::CostBasisTotal:
            return QStringLiteral("Cost basis (total)");
        case Field::AverageCost:
            return QStringLiteral("Average cost / share");
    }
    return {};
}

std::optional<Table> read_table(const QByteArray& bytes, QString* error) {
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    QVector<int> lines;
    const QChar delim = detect_delimiter(text);
    const QVector<QStringList> rows = split_rows(text, delim, &lines);
    for (int i = 0; i < rows.size(); ++i) {
        int hits = 0;
        for (const QString& c : rows[i])
            if (match(c))
                ++hits;
        if (hits < 2)
            continue;
        Table t;
        t.headers = rows[i];
        t.header_line = lines[i];
        t.decimal_comma = delim == ';';
        for (int j = i + 1; j < rows.size(); ++j)
            if (!row_blank(rows[j])) {
                t.rows << rows[j];
                t.row_lines << lines[j];
            }
        return t;
    }
    if (error)
        *error = QStringLiteral("No header row found — expected columns such as Symbol, Quantity, Cost Basis "
                                "(a positions export) or Date, Action, Symbol, Quantity, Price (an activity export).");
    return std::nullopt;
}

QHash<int, Field> guess_mapping(const QStringList& headers) {
    QHash<Field, std::pair<int, int>> best; // field → (column, rank)
    for (int col = 0; col < headers.size(); ++col) {
        const auto m = match(headers[col]);
        if (!m)
            continue;
        const auto it = best.constFind(m->first);
        if (it == best.constEnd() || m->second < it->second)
            best.insert(m->first, {col, m->second});
    }
    QHash<int, Field> out;
    for (int col = 0; col < headers.size(); ++col)
        out.insert(col, Field::Ignore);
    for (auto it = best.cbegin(); it != best.cend(); ++it)
        out.insert(it.value().first, it.key());
    return out;
}

Kind guess_kind(const QHash<int, Field>& mapping) {
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it)
        if (it.value() == Field::Action)
            return Kind::Activity;
    return Kind::Positions;
}

std::optional<double> parse_number(QString text, bool decimal_comma) {
    text = text.trimmed();
    if (text.isEmpty() || text.endsWith('%'))
        return std::nullopt;
    bool negative = false;
    if (text.startsWith('(') && text.endsWith(')')) {
        negative = true;
        text = text.mid(1, text.size() - 2);
    }
    text.remove('$').remove(' ').remove(QLatin1String("USD"));
    if (decimal_comma) {
        text.remove('.');
        text.replace(',', '.');
    } else {
        text.remove(',');
    }
    if (text.startsWith('+'))
        text.remove(0, 1);
    if (text.startsWith('-')) {
        negative = !negative;
        text.remove(0, 1);
    }
    if (text.startsWith('$'))
        text.remove(0, 1);
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v))
        return std::nullopt;
    return negative ? -v : v;
}

std::optional<QDate> parse_date(QString text) {
    text = text.trimmed();
    if (const int as_of = text.indexOf(QLatin1String(" as of"), 0, Qt::CaseInsensitive); as_of > 0)
        text = text.left(as_of).trimmed();
    for (const char* fmt : {"MM/dd/yyyy", "M/d/yyyy", "yyyy-MM-dd", "dd-MMM-yyyy", "d-MMM-yyyy", "MMM d, yyyy",
                            "dd MMM yyyy", "yyyyMMdd", "MM/dd/yy", "M/d/yy"}) {
        QDate d = QDate::fromString(text, QLatin1String(fmt));
        if (!d.isValid())
            continue;
        if (d.year() < 1970) // two-digit years: Qt reads "24" as 1924
            d = d.addYears(100);
        return d;
    }
    return std::nullopt;
}

ParseResult to_transactions(const Table& table, const QHash<int, Field>& mapping, Kind kind, const QDate& as_of) {
    ParseResult out;
    int col[16];
    std::fill(std::begin(col), std::end(col), -1);
    // Lowest-numbered column wins when a field is mapped twice — never hash order.
    QList<int> columns = mapping.keys();
    std::sort(columns.begin(), columns.end());
    for (const int c : columns) {
        const Field f = mapping.value(c);
        if (f != Field::Ignore && col[static_cast<int>(f)] < 0)
            col[static_cast<int>(f)] = c;
    }
    const auto at = [&](const QStringList& row, Field f) { return cell(row, col[static_cast<int>(f)]); };
    const auto num = [&](const QStringList& row, Field f) { return parse_number(at(row, f), table.decimal_comma); };

    if (col[static_cast<int>(Field::Symbol)] < 0) {
        out.warnings << QStringLiteral("No Symbol column is mapped — nothing to import.");
        return out;
    }
    if (col[static_cast<int>(Field::Quantity)] < 0 && kind == Kind::Positions) {
        out.warnings << QStringLiteral("No Quantity column is mapped — a positions file needs one.");
        return out;
    }

    QStringList no_cost;
    for (int r = 0; r < table.rows.size(); ++r) {
        const QStringList& row = table.rows[r];
        const int line = r < table.row_lines.size() ? table.row_lines[r] : table.header_line + r + 1;
        const QString raw_symbol = at(row, Field::Symbol);
        const auto qty = num(row, Field::Quantity);
        // Disclaimer paragraphs and section headings: nothing in the columns
        // that matter. Not worth a line in the report.
        if (raw_symbol.isEmpty() && !qty && !num(row, Field::Amount))
            continue;
        const QString why = non_security_reason(raw_symbol);
        if (!why.isEmpty()) {
            out.skipped << QStringLiteral("line %1: %2 — %3").arg(line).arg(raw_symbol.isEmpty() ? row.join(' ').left(40) : raw_symbol, why);
            continue;
        }
        const QString symbol = clean_symbol(raw_symbol);

        if (kind == Kind::Positions) {
            if (!qty || *qty <= 0) {
                out.skipped << QStringLiteral("line %1: %2 — no positive quantity (short positions aren't supported)")
                                   .arg(line)
                                   .arg(symbol);
                continue;
            }
            double cost = 0;
            if (const auto avg = num(row, Field::AverageCost); avg && *avg > 0)
                cost = *avg;
            else if (const auto total = num(row, Field::CostBasisTotal); total && *total > 0)
                cost = *total / *qty;
            else if (const auto px = num(row, Field::Price); px && *px > 0) {
                cost = *px;
                no_cost << symbol;
            } else {
                out.skipped << QStringLiteral("line %1: %2 — no cost basis or price").arg(line).arg(symbol);
                continue;
            }
            out.transactions.append({as_of.toString(Qt::ISODate), symbol, QStringLiteral("BUY"), *qty, cost,
                                      QStringLiteral("Imported holding (as of %1)").arg(as_of.toString(Qt::ISODate))});
            continue;
        }

        // Activity
        const QString action = at(row, Field::Action);
        const auto type = classify_action(action);
        if (!type || type->startsWith('!')) {
            out.skipped << QStringLiteral("line %1: %2 \"%3\" — %4")
                               .arg(line)
                               .arg(symbol, action.left(40),
                                    type ? type->mid(1) : QStringLiteral("not a buy, sell or dividend"));
            continue;
        }
        const auto date = parse_date(at(row, Field::Date));
        if (!date) {
            out.skipped << QStringLiteral("line %1: %2 — no readable date (\"%3\")").arg(line).arg(symbol, at(row, Field::Date));
            continue;
        }
        const auto amount = num(row, Field::Amount);
        const auto price = num(row, Field::Price);
        ParsedTxn t{date->toString(Qt::ISODate), symbol, *type, 0, 0, QStringLiteral("Imported: %1").arg(action.left(60))};
        if (*type == QLatin1String("DIVIDEND")) {
            // The ledger books income as quantity × price; a broker row gives
            // the cash total, so record it as 1 × amount.
            if (amount && *amount < 0) {
                out.skipped << QStringLiteral("line %1: %2 dividend of %3 — a reversal or adjustment; "
                                              "correct the dividend in the Transactions panel")
                                   .arg(line)
                                   .arg(symbol)
                                   .arg(*amount, 0, 'f', 2);
                continue;
            }
            if (amount && *amount > 0) {
                t.quantity = 1;
                t.price = *amount;
            } else if (qty && price && *qty != 0 && *price > 0) {
                t.quantity = std::abs(*qty);
                t.price = *price;
            } else {
                out.skipped << QStringLiteral("line %1: %2 dividend — no amount").arg(line).arg(symbol);
                continue;
            }
        } else {
            if (!qty || *qty == 0) {
                out.skipped << QStringLiteral("line %1: %2 %3 — no quantity").arg(line).arg(*type, symbol);
                continue;
            }
            t.quantity = std::abs(*qty);
            if (price && *price > 0)
                t.price = *price;
            else if (amount && std::abs(*amount) > 0)
                t.price = std::abs(*amount) / t.quantity; // net of fees: close enough, and said so
            else {
                out.skipped << QStringLiteral("line %1: %2 %3 — no price or amount").arg(line).arg(*type, symbol);
                continue;
            }
        }
        out.transactions.append(t);
    }
    // Activity exports run newest first. Put them in time order: the ledger
    // breaks same-date ties by insertion order, so a same-day sell listed
    // above its buy would otherwise replay against nothing held.
    if (kind == Kind::Activity && out.transactions.size() > 1 &&
        out.transactions.first().date > out.transactions.last().date)
        std::reverse(out.transactions.begin(), out.transactions.end());
    if (!no_cost.isEmpty())
        out.warnings << QStringLiteral("No cost basis for %1 — used the file's price, so their gain starts at zero.")
                            .arg(no_cost.join(", "));
    return out;
}

} // namespace finterm::portfolio::csv
