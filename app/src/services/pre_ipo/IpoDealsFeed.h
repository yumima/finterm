// src/services/pre_ipo/IpoDealsFeed.h
#pragma once
// The IPO calendar as a shared feed: one row per deal (scripts/ipo_calendar.py,
// Nasdaq merged per deal + EDGAR stage + projected pricing date), for the
// Private Market widgets that live outside the screen (a dashboard tile) and
// for the private-company ↔ deal join.
//
// The PRIVATE MARKET screen loads the calendar itself and hands every answer
// to ingest(), so the feed costs nothing while the screen is open; a widget on
// another screen calls load(), which reads the (script-cached) calendar only
// when nothing fresher has been ingested.

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

    /// Read the calendar (enriched) unless an answer under 30 min old is held.
    void load();
    /// An ipo_calendar.py answer obtained elsewhere (the screen's own load).
    /// Only an ENRICHED answer is taken (from EDGAR, or the fast pass when the
    /// script's cache holds the whole EDGAR picture): a plain fast pass has
    /// no EDGAR stages, and taking it would wipe every stage-based date.
    void ingest(const QJsonObject& root);

    const QVector<IpoDeal>& deals() const { return deals_; }
    bool loaded() const { return at_.isValid(); }
    const IpoDealIndex& index() const { return index_; }
    const IpoDeal* deal_for(const pre_ipo::PrivateCompany& c, bool* strong = nullptr) const {
        return index_.deal_for(c, strong);
    }

    static IpoDeal parse_deal(const QJsonObject& o);

  signals:
    void updated();

  private:
    explicit IpoDealsFeed(QObject* parent = nullptr);
    void read(bool enrich);
    QVector<IpoDeal> deals_;
    IpoDealIndex index_;
    QDateTime at_;
    QDateTime failed_at_; // a failed read is retried after a cool-off, not on every show
    bool inflight_ = false;
};

} // namespace finterm::services
