// src/services/pre_ipo/ClarityService.h
#pragma once
// Secondary-market prices for the Private Market screen, from Clarity's public
// pages (scripts/clarity_market.py). Clarity (formerly Hiive) is a
// FINRA-registered secondary marketplace; logged out it publishes a daily
// MODEL price per company, the company's funding history (rounds with price
// per share and post-money, tenders included) and the C50 liquidity index.
//
// What is fetched, and when:
//   index      — C50 level/history/members, page-1 browse rows, the sitemap's
//                3k company slugs. Once per 6 h (the script caches too).
//   companies  — one page per company: the C50 and browse names, plus every
//                company PreIpoService follows that the marketplace covers
//                (want()). Background batches, one request a second; a page
//                is re-read at most every 12 h.
//   fetch_now  — the company the user just opened or searched for, out of
//                band, so it never waits behind a background batch.
// Everything lands in a JSON cache (cache/pre_ipo/clarity.json) so the screen
// opens with yesterday's prices while today's load.

#include "screens/pre_ipo/PreIpoTypes.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace finterm::services {

struct ClarityMember {
    int rank = 0;
    int rank_change = 0; // + up, − down
    QString name, slug, description;
    double price = std::numeric_limits<double>::quiet_NaN();
    double chg_6m_pct = std::numeric_limits<double>::quiet_NaN();
    double last_round_valuation = std::numeric_limits<double>::quiet_NaN();
    QVector<double> sparkline; // 180 days, platform units (relative shape only)
};

struct ClarityIndex {
    double level = std::numeric_limits<double>::quiet_NaN();
    QDate updated;
    double chg_3m_pct = std::numeric_limits<double>::quiet_NaN();
    double chg_6m_pct = std::numeric_limits<double>::quiet_NaN();
    double chg_1y_pct = std::numeric_limits<double>::quiet_NaN();
    double chg_max_pct = std::numeric_limits<double>::quiet_NaN();
    QVector<pre_ipo::PricePoint> points;
    QVector<ClarityMember> members;
    bool valid() const { return !points.isEmpty() || !members.isEmpty(); }
};

/// One company page, parsed.
struct ClarityCompany {
    QString slug, name, description;
    QStringList tags;
    bool tradeable = false;
    QDate ipo_date;
    QString public_ticker;
    bool acquired = false;
    double ipo_price = std::numeric_limits<double>::quiet_NaN();
    pre_ipo::MarketQuote quote;
    QVector<pre_ipo::FinancingRound> rounds; // newest first
    QString founded, employees, hq, website;
    double total_raised = std::numeric_limits<double>::quiet_NaN();
    QStringList key_investors;
    QDateTime fetched_at;
};

class ClarityService : public QObject {
    Q_OBJECT
  public:
    static ClarityService& instance();

    /// Hydrate from the disk cache (once), then refresh the index if it is
    /// older than 6 h and resume the background page reads. Idempotent.
    void load();
    /// Pages to keep current in the background (deduplicated; order kept).
    void want(const QStringList& slugs);
    /// Read one company's page now (user opened it). No-op when it is fresh
    /// or already being read.
    void fetch_now(const QString& slug);
    /// The user's ↻: re-read the index if older than 15 min and pages older
    /// than an hour, and forget this session's "no page" / failure marks.
    void refresh();

    const ClarityIndex& index() const { return index_; }
    /// The C50 members and page-1 browse rows as list-level quotes (price,
    /// 6-month change, last-round valuation) — before any page is read.
    const QVector<ClarityMember>& listed() const { return listed_; }
    const QHash<QString, ClarityCompany>& companies() const { return companies_; }
    /// Every company slug the marketplace publishes (sitemap ∪ index ∪ browse).
    const QSet<QString>& universe() const { return universe_; }
    int universe_total() const { return universe_total_; }
    bool fetching(const QString& slug) const { return inflight_slugs_.contains(slug); }
    bool unavailable(const QString& slug) const { return unavailable_.contains(slug); }
    /// The last read of this page got no answer (not a 404) — recently.
    bool failed_recently(const QString& slug) const;
    QDateTime index_fetched_at() const { return index_at_; }
    QStringList errors() const { return errors_; }

    // Parsers (public for tests): the script's JSON → structs.
    static ClarityIndex parse_index(const QJsonObject& idx);
    static ClarityMember parse_member(const QJsonObject& m);
    static ClarityCompany parse_company(const QJsonObject& c);

  signals:
    /// Index or any company changed. Coalesced (≤ 1 per 250 ms).
    void updated();

  private:
    explicit ClarityService(QObject* parent = nullptr);
    void hydrate();
    void run_index(double max_age_h = 6.0);
    void apply_index(const QJsonObject& root);
    void pump();
    void run_companies(const QStringList& slugs, bool background);
    void apply_companies(const QJsonObject& root, const QStringList& asked);
    void save();
    void notify();

    ClarityIndex index_;
    QVector<ClarityMember> listed_;
    QHash<QString, ClarityCompany> companies_;
    QSet<QString> universe_;
    int universe_total_ = 0;
    QDateTime index_at_;
    QStringList errors_;

    QJsonObject raw_index_;                 // last good index answer (for the cache)
    QHash<QString, QJsonObject> raw_companies_; // slug → last good record

    bool hydrated_ = false;
    bool index_inflight_ = false;
    bool batch_inflight_ = false;
    QStringList queue_;                // background slugs, in order
    QSet<QString> queued_;
    /// When each page was last asked for. Re-asked after the script's own
    /// 12 h page TTL, so a session left open keeps its prices current.
    QHash<QString, qint64> asked_at_;
    /// When a read of the page last got no answer. Background reads skip it
    /// for a cool-off; a user click retries after a minute.
    QHash<QString, qint64> failed_at_;
    QSet<QString> inflight_slugs_;
    QSet<QString> unavailable_;        // the marketplace answered 404 — final for the session
    qint64 refresh_until_ms_ = 0;      // a ↻ is in effect: pages read at a 1 h TTL until then
    class QTimer* notify_timer_ = nullptr;
};

} // namespace finterm::services
