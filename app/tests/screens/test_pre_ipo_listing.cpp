// A pre-IPO list lets go of a company once it trades: a week of grace after
// the first trading day, then gone.
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QtTest>

using namespace finterm::pre_ipo;

class TestPreIpoListing : public QObject {
    Q_OBJECT
  private slots:
    void rule() {
        const QDate listed(2026, 6, 12);
        QCOMPARE(listing_state({}, {}, listed), ListingState::Private);
        QCOMPARE(listing_state({}, listed, listed.addDays(400)), ListingState::Private); // no ticker: not listed
        QCOMPARE(listing_state("SPCX", listed, listed), ListingState::JustListed);       // the IPO day itself
        QCOMPARE(listing_state("SPCX", listed, listed.addDays(7)), ListingState::JustListed);
        QCOMPARE(listing_state("SPCX", listed, listed.addDays(8)), ListingState::Listed);
        QCOMPARE(listing_state("SPCX", listed, QDate(2026, 11, 16)), ListingState::Listed); // the reported case
        // It trades, on a day the vendor does not give: not pre-IPO either way.
        QCOMPARE(listing_state("SPCX", {}, listed), ListingState::Listed);
    }
};

QTEST_APPLESS_MAIN(TestPreIpoListing)
#include "test_pre_ipo_listing.moc"
