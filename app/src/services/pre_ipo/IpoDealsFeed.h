// src/services/pre_ipo/IpoDealsFeed.h
#pragma once
// The IPO calendar as a shared feed: one row per deal (scripts/ipo_calendar.py,
// Nasdaq merged per deal + EDGAR stage + projected pricing date), for the
// Private Market widgets that live outside the screen (a dashboard tile) and
// for the private-company ↔ deal join.
//
// The ONLY runner of ipo_calendar.py. The PRIVATE MARKET screen, the News
// "What's On" panel and the dashboard IPO tile all read it: one fast pass and
// one EDGAR enrich at a time, whoever asks, every answer broadcast to all.
// (They used to run the script — or call Nasdaq directly — each on their own,
// two enrich passes could hit SEC at once, and the tile parsed raw Nasdaq rows
// differently from everyone else.)

#include "screens/pre_ipo/PreIpoTypes.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVector>

namespace finterm::services {

struct IpoDeal {
    QString deal_id, company, ticker, exchange, cik;
    QString status; // upcoming | filed | priced | withdrawn
    bool spac = false;
    QString price_range;
    double deal_size_usd = 0; // range midpoint × shares; final × shares once priced; 0 = unknown
    QDate filed, priced;
    /// Projected pricing date for a pending deal (date, else window start);
    /// the event date otherwise.
    QDate when;
    QString kind;  // projection kind (scheduled / imminent / estimate / …)
    QString basis; // what the projection rests on
    bool firm() const { return kind == "scheduled" || kind == "imminent"; }
    bool pending() const { return status == "upcoming" || status == "filed"; }
};

/// The private-company ↔ deal join over a set of deals — one set of rules
/// for the screen (over its own calendar rows) and for the feed's widgets.
class IpoDealIndex {
  public:
    void build(const QVector<IpoDeal>& deals);
    /// By CIK, else by name after the legal form. `strong` = CIK or a
    /// multi-word name. A one-word name ("Figure", "Mercury") is accepted
    /// only for a pending deal of $100M+ and is never strong — it must not be
    /// taken as proof the company is public.
    const IpoDeal* deal_for(const pre_ipo::PrivateCompany& c, bool* strong = nullptr) const;
    bool empty() const { return deals_.isEmpty(); }

  private:
    QVector<IpoDeal> deals_;
    QHash<QString, int> by_key_; // "cik:<digits>" / "name:<slug>" → best deal
};

class IpoDealsFeed : public QObject {
    Q_OBJECT
  public:
    static IpoDealsFeed& instance();

    /// Read the calendar unless an answer under 30 min old is held (`force`
    /// = a user's refresh). The fast pass is followed by an EDGAR enrich only
    /// when the EDGAR picture is over 30 min old — a reader that just wants
    /// the rows must not start minutes of SEC reads in the shared queue.
    void load(bool force = false);
    /// Run one pass now unless that pass is already out (its answer will be
    /// broadcast to everyone). `chain_enrich`: follow a fast pass with an
    /// enrich regardless (the PRIVATE MARKET screen, which shows the stages).
    void request(bool enrich, bool chain_enrich = false);
    bool inflight(bool enrich) const { return enrich ? enrich_inflight_ : fast_inflight_; }

    /// The last calendar answer held — enriched if one has landed (a later
    /// plain fast pass never replaces it: it has no EDGAR stages), else the
    /// fast pass. Empty until the first answer.
    const QJsonObject& root() const { return root_; }
    bool enriched() const { return root_.value("enriched").toBool(); }
    /// The newest answer of either kind — what a list of rows wants (a plain
    /// fast pass carries today's Nasdaq pricings even while EDGAR is failing).
    const QJsonObject& latest_root() const { return latest_.isEmpty() ? root_ : latest_; }

    const QVector<IpoDeal>& deals() const { return deals_; }
    bool loaded() const { return !root_.isEmpty(); }
    const IpoDealIndex& index() const { return index_; }
    const IpoDeal* deal_for(const pre_ipo::PrivateCompany& c, bool* strong = nullptr) const {
        return index_.deal_for(c, strong);
    }

    static IpoDeal parse_deal(const QJsonObject& o);

  signals:
    /// root()/deals() changed.
    void updated();
    /// Every pass's outcome, for a surface that reports its own status: the
    /// raw answer when ok, else `why`.
    void answered(bool enrich, bool ok, const QJsonObject& root, const QString& why);

  private:
    explicit IpoDealsFeed(QObject* parent = nullptr);
    void take(const QJsonObject& root, bool persist = true);
    QJsonObject root_;
    QJsonObject latest_;
    QVector<IpoDeal> deals_;
    IpoDealIndex index_;
    QDateTime enriched_at_; // when EDGAR was actually read (the answer's own stamp)
    QDateTime answered_at_; // the last answer of any kind
    QDateTime failed_at_; // a failed read is retried after a cool-off, not on every show
    bool fast_inflight_ = false;
    bool enrich_inflight_ = false;
};

} // namespace finterm::services
