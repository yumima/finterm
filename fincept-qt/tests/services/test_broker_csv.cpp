// tests/services/test_broker_csv.cpp
//
// Broker CSV exports → portfolio transactions. The files below are shaped
// like the real exports (preamble lines above the header, "$1,234.56"
// numbers, total / cash / pending / option rows mixed in with holdings), with
// made-up accounts and amounts.

#include "services/portfolio/BrokerCsv.h"

#include <QtTest/QtTest>

using namespace fincept::portfolio::csv;

namespace {

ParseResult parse(const QByteArray& bytes, const QDate& as_of = QDate(2026, 11, 13)) {
    QString err;
    const auto t = read_table(bytes, &err);
    if (!t)
        qFatal("read_table failed: %s", qPrintable(err));
    const auto m = guess_mapping(t->headers);
    return to_transactions(*t, m, guess_kind(m), as_of);
}

} // namespace

class TestBrokerCsv : public QObject {
    Q_OBJECT

  private slots:
    void numbers() {
        QCOMPARE(parse_number("$1,234.56").value(), 1234.56);
        QCOMPARE(parse_number("(12.30)").value(), -12.3);
        QCOMPARE(parse_number("-$5.00").value(), -5.0);
        QCOMPARE(parse_number("+3").value(), 3.0);
        QVERIFY(!parse_number("--"));
        QVERIFY(!parse_number("n/a"));
        QVERIFY(!parse_number("4.5%"));
        QVERIFY(!parse_number(""));
    }

    void dates() {
        QCOMPARE(parse_date("11/12/2026").value(), QDate(2026, 11, 12));
        QCOMPARE(parse_date("1/5/2026").value(), QDate(2026, 1, 5));
        QCOMPARE(parse_date("2026-01-05").value(), QDate(2026, 1, 5));
        QCOMPARE(parse_date("03/04/26").value(), QDate(2026, 3, 4)); // not 1926
        QCOMPARE(parse_date("10/31/2026 as of 10/30/2026").value(), QDate(2026, 10, 31));
        QVERIFY(!parse_date("Pending"));
    }

    // A positions export: preamble above the header, quoted thousands, a
    // money-market sweep, an option, cash and total rows.
    void positions_file() {
        const QByteArray f = "\"Positions for account Individual ...123 as of 11/13/2026\"\n"
                             "\n"
                             "\"Symbol\",\"Description\",\"Qty (Quantity)\",\"Price\",\"Cost Basis\",\"Mkt Val (Market Value)\"\n"
                             "\"AAPL\",\"APPLE INC\",\"10\",\"$230.00\",\"$1,500.00\",\"$2,300.00\"\n"
                             "\"BRK/B\",\"BERKSHIRE HATHAWAY B\",\"2\",\"$480.00\",\"$700.00\",\"$960.00\"\n"
                             "\"SPAXX**\",\"MONEY MARKET\",\"500\",\"$1.00\",\"--\",\"$500.00\"\n"
                             "\"AAPL 01/16/2027 250.00 C\",\"CALL APPLE\",\"1\",\"$5.00\",\"$300.00\",\"$500.00\"\n"
                             "\"Cash & Cash Investments\",\"--\",\"--\",\"--\",\"--\",\"$1,000.00\"\n"
                             "\"Account Total\",\"--\",\"--\",\"--\",\"$2,500.00\",\"$5,260.00\"\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 2);
        QCOMPARE(r.transactions[0].symbol, QStringLiteral("AAPL"));
        QCOMPARE(r.transactions[0].type, QStringLiteral("BUY"));
        QCOMPARE(r.transactions[0].quantity, 10.0);
        QCOMPARE(r.transactions[0].price, 150.0); // cost basis / qty, not market price
        QCOMPARE(r.transactions[0].date, QStringLiteral("2026-11-13"));
        QCOMPARE(r.transactions[1].symbol, QStringLiteral("BRK-B"));
        QCOMPARE(r.transactions[1].price, 350.0);
        QCOMPARE(r.skipped.size(), 4); // sweep, option, cash, total — each reported
    }

    void positions_average_cost_wins() {
        const QByteArray f = "Account Name,Symbol,Quantity,Last Price,Cost Basis Total,Average Cost Basis\n"
                             "Brokerage,MSFT,4,$500.00,\"$1,200.00\",$310.00\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 1);
        QCOMPARE(r.transactions[0].price, 310.0);
    }

    void positions_without_cost_fall_back_and_say_so() {
        const QByteArray f = "Symbol,Shares,Share Price\nVTI,3,$300.00\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 1);
        QCOMPARE(r.transactions[0].price, 300.0);
        QVERIFY(!r.warnings.isEmpty());
    }

    // An activity export: buys, sells with negative quantity, a reinvested
    // dividend (cash leg + share leg), a transfer and a split.
    void activity_file() {
        const QByteArray f =
            "\"Date\",\"Action\",\"Symbol\",\"Description\",\"Quantity\",\"Price\",\"Fees & Comm\",\"Amount\"\n"
            "\"11/03/2026\",\"Buy\",\"NVDA\",\"NVIDIA\",\"5\",\"$180.00\",\"\",\"-$900.00\"\n"
            "\"11/04/2026 as of 11/03/2026\",\"Sell\",\"NVDA\",\"NVIDIA\",\"-2\",\"$190.00\",\"$0.01\",\"$379.99\"\n"
            "\"11/05/2026\",\"Reinvest Dividend\",\"VTI\",\"VANGUARD\",\"\",\"\",\"\",\"$12.34\"\n"
            "\"11/05/2026\",\"Reinvest Shares\",\"VTI\",\"VANGUARD\",\"0.041\",\"$301.00\",\"\",\"-$12.34\"\n"
            "\"11/06/2026\",\"Qualified Dividend\",\"MSFT\",\"MICROSOFT\",\"\",\"\",\"\",\"$8.00\"\n"
            "\"11/07/2026\",\"Journal\",\"\",\"TRANSFER\",\"\",\"\",\"\",\"$100.00\"\n"
            "\"11/08/2026\",\"Stock Split\",\"TSLA\",\"TESLA\",\"10\",\"\",\"\",\"\"\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 5);
        QCOMPARE(r.transactions[0].type, QStringLiteral("BUY"));
        QCOMPARE(r.transactions[1].type, QStringLiteral("SELL"));
        QCOMPARE(r.transactions[1].quantity, 2.0); // sign dropped, the type says sell
        QCOMPARE(r.transactions[1].date, QStringLiteral("2026-11-04"));
        QCOMPARE(r.transactions[2].type, QStringLiteral("DIVIDEND"));
        QCOMPARE(r.transactions[2].quantity * r.transactions[2].price, 12.34);
        QCOMPARE(r.transactions[3].type, QStringLiteral("BUY")); // the share leg
        QCOMPARE(r.transactions[4].symbol, QStringLiteral("MSFT"));
        QCOMPARE(r.skipped.size(), 2); // journal (no symbol) and split (ratio unknown) — reported
    }

    void price_from_amount_when_missing() {
        const QByteArray f = "Trade Date,Transaction Type,Symbol,Shares,Net Amount\n"
                             "2026-10-01,Buy,AMZN,4,\"-$800.00\"\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 1);
        QCOMPARE(r.transactions[0].price, 200.0);
    }

    void semicolon_delimited_and_bom() {
        const QByteArray f = "\xEF\xBB\xBFSymbol;Quantity;Average Cost\nASML;1;700,00\n";
        const auto t = read_table(f);
        QVERIFY(t);
        QCOMPARE(t->headers.size(), 3);
    }

    void bare_cash_is_a_ticker() {
        const QByteArray f = "Symbol,Quantity,Average Cost\nCASH,10,$60.00\n";
        QCOMPARE(parse(f).transactions.size(), 1);
    }

    // Exports list activity newest first; a same-day sell above its buy must
    // replay after it.
    void activity_put_in_time_order() {
        const QByteArray f = "Date,Action,Symbol,Quantity,Price\n"
                             "03/02/2026,Sell,AAPL,5,$200.00\n"
                             "03/01/2026,Sell,AAPL,5,$190.00\n"
                             "03/01/2026,Buy,AAPL,10,$180.00\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 3);
        QCOMPARE(r.transactions[0].type, QStringLiteral("BUY"));
        QCOMPARE(r.transactions[2].date, QStringLiteral("2026-03-02"));
    }

    void shorts_splits_and_reversals_are_reported_not_booked() {
        const QByteArray f = "Date,Action,Symbol,Quantity,Price,Amount\n"
                             "03/01/2026,Sell Short,TSLA,50,$200.00,$10000.00\n"
                             "03/02/2026,Buy to Cover,TSLA,50,$190.00,-$9500.00\n"
                             "03/03/2026,Stock Split,NVDA,300,,\n"
                             "03/04/2026,Stock Dividend,KO,2,,\n"
                             "03/05/2026,Qualified Dividend,MSFT,,,-$42.10\n"
                             "03/06/2026,SHORT-TERM CAP GAIN,VTI,,,$5.00\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 0);
        QCOMPARE(r.skipped.size(), 6);
    }

    void decimal_comma_in_semicolon_files() {
        const QByteArray f = "Symbol;Quantity;Average Cost\nASML;1,5;1.234,50\n";
        const auto r = parse(f);
        QCOMPARE(r.transactions.size(), 1);
        QCOMPARE(r.transactions[0].quantity, 1.5);
        QCOMPARE(r.transactions[0].price, 1234.5);
    }

    void reported_lines_are_file_lines() {
        const QByteArray f = "Symbol,Quantity,Average Cost\n\nAAPL,1,$1.00\n\n\"Account Total\",,\n";
        const auto r = parse(f);
        QCOMPARE(r.skipped.size(), 1);
        QVERIFY2(r.skipped[0].startsWith(QStringLiteral("line 5:")), qPrintable(r.skipped[0]));
    }

    void field_mapped_twice_takes_first_column() {
        const QByteArray f = "Symbol,Quantity,Average Cost,Other\nAAPL,1,$10.00,$99.00\n";
        const auto t = read_table(f);
        auto m = guess_mapping(t->headers);
        m.insert(3, Field::AverageCost);
        const auto r = to_transactions(*t, m, Kind::Positions, QDate(2026, 1, 1));
        QCOMPARE(r.transactions[0].price, 10.0);
    }

    void no_header_is_an_error() {
        QString err;
        QVERIFY(!read_table("just,some,numbers\n1,2,3\n", &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBrokerCsv)
#include "test_broker_csv.moc"
