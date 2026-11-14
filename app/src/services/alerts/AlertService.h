#pragma once
// AlertService — the user's own alerts (price crossings, day moves, insider
// cluster buys), evaluated in-process while finterm runs and delivered through
// NotificationService (bell, toast, the external channels the user set up).
//
// There is no background daemon: when the app is closed nothing is watched.
// Rules and their fired state live in the signed-in user's database (the
// settings table, category "alerts"), so a restart neither forgets a rule nor
// re-announces something already announced.
//
// Inputs, and why each is safe to fire on:
//   • market:quote:<sym> from the DataHub — only once MarketDataService has
//     polled that quote in THIS process (has_live_quote): a disk-hydrated quote
//     can be a previous session's. A refused poll publishes nothing, so it can
//     never read as "no move".
//   • MarketDataService::stream_ticks for extended-hours rules — each print
//     self-checks against the polled quote's close (AlertRules.h,
//     extended_print_ok) before it is used.
//   • ownership_watch.py cluster_rows — the LOCAL Form 4 store, no network. The
//     store is kept current by the Ownership feature's existing daily EDGAR
//     read (OwnershipService::ensure_form4_current); this service only nudges
//     that read when the store is already warm (an incremental day or two), so
//     it adds no SEC traffic of its own.
//
// The pure rules (firing, re-arm, once-per-session, dedupe, persistence) are
// in AlertRules.h. Evaluation is a handful of comparisons per quote on the GUI
// thread; the store is written only when a rule's state actually changes.

#include "services/alerts/AlertRules.h"
#include "services/notifications/NotificationService.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <optional>

namespace finterm::services {
struct QuoteData;
struct StreamTick;
} // namespace finterm::services

namespace finterm::alerts {

class AlertService : public QObject {
    Q_OBJECT
  public:
    static AlertService& instance();

    /// Load the signed-in user's rules and start watching. Idempotent; called
    /// once the terminal is unlocked. Re-reads the store if the user changed.
    void start();
    /// Stop watching and forget the in-memory rules (sign-out).
    void stop();
    bool running() const { return running_; }

    // ── Rules ───────────────────────────────────────────────────────────────
    QVector<Rule> rules() const { return set_.rules; }
    RuleState state(const QString& id) const { return set_.states.value(id); }
    std::optional<Rule> rule(const QString& id) const;
    /// Adds a valid rule (id and created are filled in); returns its id, or
    /// an empty string when the rule is invalid.
    QString add_rule(Rule r);
    /// Replaces the rule with the same id. The rule restarts (state Pending):
    /// a changed level must not inherit the old level's armed side.
    bool update_rule(const Rule& r);
    void set_paused(const QString& id, bool paused);
    void remove_rule(const QString& id);

    /// The one-toggle bulk rule: insider cluster buys on every watchlist and
    /// portfolio name.
    bool own_names_insider_on() const;
    void set_own_names_insider(bool on);

    /// True when an active insider-cluster rule covers `symbol` — the legacy
    /// any-insider-buy alert defers to it for cluster buys, so one filing is
    /// not announced twice.
    bool covers_insider_clusters(const QString& symbol) const;

    /// The last usable price this process saw for `symbol` (a polled quote),
    /// for prefilling the add-alert dialog. nullopt when none.
    std::optional<double> current_price(const QString& symbol) const;

    /// Whether Settings › Notifications lets each family through (cached,
    /// refreshed every minute and when the manager opens).
    bool price_delivery_on() const { return price_on_; }
    bool insider_delivery_on() const { return insider_on_; }
    void refresh_delivery_switches();

    /// What the Form 4 store said on the last insider check: the newest day
    /// it has read ("" before the first check, or when the check failed).
    QString form4_scanned_to() const { return form4_scanned_to_; }

    /// Extension point for date-based reminders (earnings dates once the
    /// calendar change lands): a source decides the rule is due and calls
    /// this. Records last_fired and delivers through NotificationService.
    void fire(const QString& rule_id, const QString& title, const QString& message, const QString& symbol,
              notifications::NotifTrigger trigger, double value = kNaN);

  signals:
    void rules_changed();
    void state_changed(const QString& rule_id);

  private:
    AlertService();

    bool load(); ///< false when the store could not be read (nothing is changed)
    void save_rules();
    void save_states();
    bool store_is_ours() const;

    void resubscribe();
    /// Evaluate every watched symbol against the hub's last polled quote. The
    /// hub does not re-publish an unchanged quote, so a rule added (or an app
    /// started) while the market is quiet would otherwise wait for the next
    /// price change to see its first price. Idempotent: the same quote cannot
    /// fire twice.
    void sweep_cached();
    void on_quote(const QString& symbol, const services::QuoteData& q);
    void on_ticks(const QVector<services::StreamTick>& ticks);
    void check_insiders();
    void deliver_cross(const Rule& r, double price, const QString& source);
    void deliver_move(const Rule& r, double change_pct, double baseline, double price);
    QSet<QString> insider_symbols() const;
    bool has_active_insider_rule() const;
    RuleState& state_ref(const QString& id) { return set_.states[id]; }

    RuleSet set_;
    bool running_ = false;
    QString db_path_; ///< the user database the rules were read from
    QSet<QString> quote_syms_; ///< market:quote topics subscribed
    QSet<QString> first_seen_; ///< rule ids that have seen a price this process
    /// Per symbol: the last stream print was outside the regular session, so
    /// the polled (regular) price is the previous close, not the tape — an
    /// extended-hours rule must not see-saw between the two. Consulted only
    /// outside US regular hours: during the session the quote IS the tape,
    /// even if the stream dropped after pre-market.
    QHash<QString, bool> in_extended_;
    bool price_on_ = true;
    bool insider_on_ = true;
    bool insider_checking_ = false;
    bool insider_recheck_ = false;
    bool nudged_scan_ = false;
    QString form4_scanned_to_;
    QTimer* insider_timer_ = nullptr;
    QTimer* switches_timer_ = nullptr;
};

} // namespace finterm::alerts
