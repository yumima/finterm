// src/services/pre_ipo/PreIpoService.cpp
#include "services/pre_ipo/PreIpoService.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTimeZone>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fincept::services {

using namespace fincept::pre_ipo;

namespace {

QString sector_for_industry(const QString& industry_group) {
    if (industry_group.isEmpty()) return {};
    const QString g = industry_group.toLower();
    if (g.contains("technology") || g.contains("computer") || g.contains("software"))
        return "Technology";
    if (g.contains("biotech") || g.contains("pharma") || g.contains("health") || g.contains("life sciences"))
        return "Healthcare";
    if (g.contains("financial") || g.contains("bank") || g.contains("insurance"))
        return "Financials";
    if (g.contains("energy") || g.contains("oil") || g.contains("renewable"))
        return "Energy";
    if (g.contains("real estate"))
        return "Real Estate";
    if (g.contains("retail") || g.contains("consumer"))
        return "Consumer";
    if (g.contains("manufacturing") || g.contains("industrial") || g.contains("aerospace") || g.contains("defense"))
        return "Industrials";
    return industry_group;
}

QStringList tags_for_company(const PrivateCompany& c) {
    QStringList out;
    const QString s = (c.sector + " " + c.sub_sector + " " + c.description).toLower();
    auto maybe = [&](const QString& tag, const QStringList& kws) {
        for (const auto& k : kws) if (s.contains(k)) { out << tag; return; }
    };
    maybe("ai",       {"ai", "artificial intelligence", "machine learning", "llm"});
    maybe("fintech",  {"fintech", "payments", "financial", "banking"});
    maybe("biotech",  {"biotech", "pharma", "therapeutics", "drug"});
    maybe("defense",  {"defense", "aerospace", "military"});
    maybe("crypto",   {"crypto", "blockchain", "web3"});
    maybe("saas",     {"saas", "cloud", "software"});
    out.removeDuplicates();
    return out;
}

// Lowercase, punctuation→single space, trimmed. For fuzzy name matching.
QString norm_name(const QString& s) {
    QString out = s.toLower();
    static const QRegularExpression non_alnum("[^a-z0-9]+");
    out.replace(non_alnum, " ");
    return out.simplified();
}

// Slugify mirroring the Python _slug (lowercase, non-alnum→'-', trim '-').
QString slugify(const QString& s) {
    QString out = s.toLower();
    static const QRegularExpression non_alnum("[^a-z0-9]+");
    out.replace(non_alnum, "-");
    while (out.endsWith('-')) out.chop(1);
    while (out.startsWith('-')) out.remove(0, 1);
    return out;
}

// Fold a duplicate company entry (`other`) into `primary`, keeping primary's
// non-empty fields and pulling in other's only where primary is empty. Used to
// reconcile a canonical-slug stub (e.g. "spacex" from the SPV/N-PORT layer)
// with the Form-D legal-name entry that describes the same company.
//
// Intentionally NOT merged because they're rebuilt downstream every emit and
// would be stale here: `analytics` (recompute_analytics), `spv_activity`
// (attach_spv_activity), `seed_*`/`last_valuation_usd` (seed_apply_valuation),
// `revenue_est_usd`/`s1_filed_date`/`ipo_expected_window` (recompute_analytics).
// A NEW value-bearing PrivateCompany field that is NOT in one of those rebuild
// passes must be added below, or it will be lost when `other` is removed.
void merge_company_into(PrivateCompany& primary, const PrivateCompany& other) {
    if (primary.cik.isEmpty())        primary.cik = other.cik;
    if (primary.name.isEmpty())       primary.name = other.name;
    if (primary.sector.isEmpty())     primary.sector = other.sector;
    if (primary.sub_sector.isEmpty()) primary.sub_sector = other.sub_sector;
    if (primary.hq_city.isEmpty())    primary.hq_city = other.hq_city;
    if (primary.hq_state.isEmpty())   primary.hq_state = other.hq_state;
    if (primary.hq_country.isEmpty()) primary.hq_country = other.hq_country;
    if (!primary.founded.isValid())   primary.founded = other.founded;
    if (primary.employee_count_est == 0) primary.employee_count_est = other.employee_count_est;
    if (primary.key_investors.isEmpty()) primary.key_investors = other.key_investors;
    if (primary.cumulative_raised_m < other.cumulative_raised_m)
        primary.cumulative_raised_m = other.cumulative_raised_m;
    if (primary.rounds.isEmpty())     primary.rounds = other.rounds;
    if (primary.fund_marks.isEmpty()) primary.fund_marks = other.fund_marks;
    if (primary.secondary.isEmpty())  primary.secondary = other.secondary;
    if (!primary.s1.first_filed.isValid() && other.s1.first_filed.isValid())
        primary.s1 = other.s1;
    if (primary.fin.revenue_m == 0 && other.fin.revenue_m != 0) primary.fin = other.fin;
    if (!primary.last_round_date.isValid() && other.last_round_date.isValid()) {
        primary.last_round_date = other.last_round_date;
        primary.last_round_name = other.last_round_name;
    }
    primary.watched = primary.watched || other.watched;
    // IpoStatus enum is ordered Unknown<Rumored<Filed<Priced<Listed<Acquired,
    // so the larger value is the more-advanced status.
    if (static_cast<int>(other.ipo_status) > static_cast<int>(primary.ipo_status))
        primary.ipo_status = other.ipo_status;
    for (const auto& t : other.tags) if (!primary.tags.contains(t)) primary.tags << t;
    for (const auto& a : other.aliases) if (!primary.aliases.contains(a)) primary.aliases << a;
    if (!other.id.isEmpty() && other.id != primary.id && !primary.aliases.contains(other.id))
        primary.aliases << other.id;
}

} // namespace

// ── Singleton ────────────────────────────────────────────────────────────────

PreIpoService& PreIpoService::instance() {
    static PreIpoService inst;
    return inst;
}

PreIpoService::PreIpoService(QObject* parent) : QObject(parent) {
    // Pre-warm cadence: once the service is alive (= user has opened the
    // Pre-IPO screen at least once this app session), fire a background
    // refresh every day at 05:00 America/New_York. That's after EDGAR's
    // 22:00 ET filing window has closed and before the user typically
    // opens the terminal, so cached data is ready by morning. If the app
    // is asleep at 05:00 ET, the min-check gate in load_data() picks up the
    // slack on the next click.
    load_cursors();
    load_valuation_seed();
    schedule_next_daily_refresh();
}

// ── Public API ────────────────────────────────────────────────────────────────

void PreIpoService::load_data() {
    if (loading_) return;
    //   1. Hydrate from disk cache (if any) and emit immediately so views
    //      populate with no network wait.
    //   2. Anti-thrash gate: if we ran a check very recently (< kMinCheckSecs),
    //      don't even probe — pre-IPO data can't meaningfully change in hours,
    //      and this stops every window-focus from hitting EDGAR.
    //   3. Otherwise refresh in the background. Refresh is now CHEAP when
    //      nothing changed: each source runs a data-based cursor probe and
    //      short-circuits unless EDGAR shows new filings (see refresh_internal).
    const bool had_cache = load_from_cache();
    if (had_cache) {
        loaded_ = true;
        emit_summary();
    }

    // kMinCheckSecs is a short anti-thrash window, NOT a data-staleness TTL:
    // it caps how often we bother to *probe*, while the actual fetch decision
    // is data-based. last_checked updates on every probe (even when nothing
    // changed), so stable data doesn't force a re-probe on every open.
    constexpr qint64 kMinCheckSecs = 6 * 60 * 60;
    const QDateTime last_checked =
        QDateTime::fromString(cursors_.value(QStringLiteral("last_checked")).toString(),
                              Qt::ISODate);
    if (had_cache && last_checked.isValid() &&
        last_checked.secsTo(QDateTime::currentDateTime()) < kMinCheckSecs) {
        LOG_INFO("PreIpo",
                 QString("Checked %1 min ago — skipping probe (data-based gate).")
                     .arg(last_checked.secsTo(QDateTime::currentDateTime()) / 60));
        return;
    }

    if (had_cache) {
        emit progress(QStringLiteral("Checking SEC for new filings…"));
    }
    refresh_internal(/*force*/ false);
}

void PreIpoService::refresh() { refresh_internal(/*force*/ true); }

void PreIpoService::refresh_internal(bool force) {
    if (!python::PythonRunner::instance().is_available()) {
        LOG_WARN("PreIpo", "Python not available — cannot load private market data");
        emit error_occurred(QStringLiteral("Python runtime unavailable."));
        return;
    }
    if (loading_) return;
    loading_ = true;
    force_refresh_ = force;
    pending_bits_ = FB_All;
    failed_bits_  = 0;

    // Stamp the probe time up front (and persist) so a crash or rapid re-open
    // mid-fetch doesn't immediately re-probe.
    cursors_[QStringLiteral("last_checked")] =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    save_cursors();
    // Only show the "Loading…" status if we don't already have cached
    // rows on screen. When the cache hydrated, load_data() has already
    // emitted "Loaded from cache · refreshing in background…" — letting
    // that stand keeps the user oriented (data is here, fresher is on
    // the way) instead of flipping back to a generic Loading message
    // for the 30-120s the network refresh takes.
    if (!loaded_) {
        emit progress(QStringLiteral("Loading SEC Form D, IPO pipeline, fund marks…"));
    }

    run_form_d_fetch();
    run_nport_marks_fetch();
    run_s1_pipeline_fetch();

    // Deep SPV scan runs independently of the 3-bit SEC flow (like Nasdaq /
    // Finnhub below): its ~2-min targeted full-text search must never block or
    // fail the core Form D load. Results land in spv_raw_ and are re-joined to
    // companies on every emit_summary().
    run_spv_fetch();

    // Nasdaq IPO calendar runs independently — no auth, fast, enriches the
    // pipeline with confirmed dates and catches companies not yet in EDGAR.
    const QDate today = QDate::currentDate();
    run_nasdaq_ipo_fetch(today.toString("yyyy-MM"));
    run_nasdaq_ipo_fetch(today.addMonths(1).toString("yyyy-MM"));

    // Finnhub augmentation — multi-exchange IPO calendar + lockup expiries.
    // Independent of all the other fetches; no-op when no API key. The
    // results live in finnhub_ipos_/finnhub_lockups_ alongside (not merged
    // into) the SEC-sourced pipeline_, so the UI can render Finnhub-sourced
    // rows with a distinct badge and the SEC source-of-truth stays clean.
    run_finnhub_calendar_fetch();
}

QVector<PrivateCompany> PreIpoService::companies() const { return companies_; }

PrivateCompany PreIpoService::company(const QString& id) const {
    for (const auto& c : companies_)
        if (c.id == id) return c;
    return {};
}

void PreIpoService::toggle_watch(const QString& id) {
    // NOTE: the IPO Watch UI does NOT use this — its watchlist is the
    // SettingsRepository-backed `starred_` set in IpoWatchView, keyed by
    // company id. PrivateCompany::watched is left here for other callers but is
    // intentionally not the source of truth for the UI star (avoids two
    // divergent watch states). See IpoWatchView::toggle_private_star.
    for (auto& c : companies_) {
        if (c.id == id) {
            c.watched = !c.watched;
            emit company_updated(id);
            return;
        }
    }
}

QVector<FormDFiling> PreIpoService::recent_form_d() const { return form_d_; }
QVector<S1Filing>    PreIpoService::ipo_pipeline()  const { return pipeline_; }
QVector<Signal>      PreIpoService::signal_list()   const { return signals_; }
QVector<FundEntry>   PreIpoService::funds()         const { return funds_; }

QVector<services::finnhub::FinnhubIPO>    PreIpoService::finnhub_ipos()    const { return finnhub_ipos_; }
QVector<services::finnhub::FinnhubLockup> PreIpoService::finnhub_lockups() const { return finnhub_lockups_; }

// ── Fetchers ─────────────────────────────────────────────────────────────────

void PreIpoService::run_form_d_fetch() {
    // The Form D *discovery sweep* is the one source with no cheap data marker
    // — market-wide Form D activity advances every day, so a count cursor would
    // never let it skip. It's therefore the lone time-backstopped source: run
    // at most once / 24h (unless forced). This is the legitimate, narrow use of
    // a clock that we agreed remains under the data-based design.
    if (!force_refresh_) {
        const QDateTime ts = QDateTime::fromString(
            cursors_.value(QStringLiteral("form_d")).toObject()
                    .value(QStringLiteral("ts")).toString(), Qt::ISODate);
        if (ts.isValid() && ts.secsTo(QDateTime::currentDateTime()) < 24 * 60 * 60) {
            LOG_INFO("PreIpo", "Form D discovery sweep < 24h old — skipping.");
            pending_bits_ &= ~FB_FormD;
            if (pending_bits_ == 0) finalize_load();
            return;
        }
    }

    QPointer<PreIpoService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form_d_data.py"),
        // parse_xml_max=30: SEC asks for ≤10 req/s shared User-Agent and
        // PreIpoService runs 3 scripts concurrently, so each XML parse
        // costs ~2 requests × 0.4s gap ≈ 0.8s. At 120 parses that's ~96s
        // of just rate-limit waits — the dominant first-load cost. 30
        // parses ≈ 24s and is enough to filter the most-recent operating-
        // company filings; the long tail of older filings retains its
        // unfiltered display name until the next refresh widens.
        {QStringLiteral("all_data"),
         QStringLiteral("{\"days_back_fd\":365,\"parse_xml_max\":30,\"include_known\":true}")},
        [self](python::PythonResult result) {
            if (!self) return;
            bool parsed = false;
            QJsonDocument doc;
            if (result.success && !result.output.trimmed().isEmpty()) {
                const QString js = python::extract_json(result.output);
                doc = QJsonDocument::fromJson(js.toUtf8());
                if (doc.isObject()) {
                    self->parse_form_d_response(doc.object());
                    self->save_cache(QStringLiteral("form_d.json"), doc);
                    QJsonObject fc;
                    fc[QStringLiteral("ts")] =
                        QDateTime::currentDateTime().toString(Qt::ISODate);
                    self->cursors_[QStringLiteral("form_d")] = fc;
                    self->save_cursors();
                    parsed = true;
                }
            }
            if (!parsed) {
                LOG_WARN("PreIpo", "Form D fetch failed: " + result.error.left(200));
                self->failed_bits_ |= FB_FormD;
            }
            self->pending_bits_ &= ~FB_FormD;
            // Emit progressively so the user sees Form D companies as soon
            // as this source returns, instead of waiting for all three.
            self->emit_summary();
            if (self->pending_bits_ == 0) self->finalize_load();
        },
        /*stream*/ {}, /*timeout*/ 120'000);
}

void PreIpoService::run_nport_marks_fetch() {
    QPointer<PreIpoService> self = this;
    // First load: cap to 1 quarter back and 6 fund families so we don't
    // blow past the 180s timeout. Subsequent refreshes (with cache hot)
    // can afford to widen — but for now, fast first-paint matters more
    // than mark depth.
    QJsonObject base;
    base[QStringLiteral("quarters_back")] = 1;
    base[QStringLiteral("families_max")]  = 6;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_nport_marks.py"),
        {QStringLiteral("marks_all"), cursor_payload(base, QStringLiteral("marks"))},
        [self](python::PythonResult result) {
            if (!self) return;
            bool parsed = false;
            if (result.success && !result.output.trimmed().isEmpty()) {
                const QString js = python::extract_json(result.output);
                const QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8());
                if (doc.isObject()) {
                    const QJsonObject o = doc.object();
                    if (o.value(QStringLiteral("unchanged")).toBool()) {
                        LOG_INFO("PreIpo", "N-PORT marks unchanged — no new fund filings.");
                        parsed = true;  // success, just nothing new
                    } else {
                        self->parse_marks_response(o);
                        self->save_cache(QStringLiteral("marks.json"), doc);
                        if (o.contains(QStringLiteral("cursor"))) {
                            self->cursors_[QStringLiteral("marks")] = o.value(QStringLiteral("cursor"));
                            self->save_cursors();
                        }
                        self->emit_summary();
                        parsed = true;
                    }
                }
            }
            if (!parsed) {
                LOG_WARN("PreIpo", "N-PORT marks fetch failed: " + result.error.left(200));
                self->failed_bits_ |= FB_Marks;
            }
            self->pending_bits_ &= ~FB_Marks;
            if (self->pending_bits_ == 0) self->finalize_load();
        },
        /*stream*/ {}, /*timeout*/ 240'000);
}

void PreIpoService::run_s1_pipeline_fetch() {
    QPointer<PreIpoService> self = this;
    QJsonObject base;
    base[QStringLiteral("days_back")] = 180;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_s1_pipeline.py"),
        {QStringLiteral("pipeline_all"), cursor_payload(base, QStringLiteral("s1"))},
        [self](python::PythonResult result) {
            if (!self) return;
            bool parsed = false;
            if (result.success && !result.output.trimmed().isEmpty()) {
                const QString js = python::extract_json(result.output);
                const QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8());
                // pipeline_all now returns an object: {"unchanged":true} or
                // {"pipeline":[...], "cursor":{...}}.
                if (doc.isObject()) {
                    const QJsonObject o = doc.object();
                    if (o.value(QStringLiteral("unchanged")).toBool()) {
                        LOG_INFO("PreIpo", "IPO pipeline unchanged — no new S-1/F-1 filings.");
                        parsed = true;
                    } else {
                        const QJsonArray arr = o.value(QStringLiteral("pipeline")).toArray();
                        self->parse_pipeline_response(arr);
                        // Cache the bare array so load_from_cache (which expects
                        // an array) keeps working unchanged.
                        self->save_cache(QStringLiteral("pipeline.json"), QJsonDocument(arr));
                        if (o.contains(QStringLiteral("cursor"))) {
                            self->cursors_[QStringLiteral("s1")] = o.value(QStringLiteral("cursor"));
                            self->save_cursors();
                        }
                        self->emit_summary();
                        parsed = true;
                    }
                }
            }
            if (!parsed) {
                LOG_WARN("PreIpo", "S-1 pipeline fetch failed: " + result.error.left(200));
                self->failed_bits_ |= FB_S1;
            }
            self->pending_bits_ &= ~FB_S1;
            if (self->pending_bits_ == 0) self->finalize_load();
        },
        /*stream*/ {}, /*timeout*/ 120'000);
}

// ── Deep SPV secondary-interest scan ──────────────────────────────────────────
//
// Targeted EDGAR full-text searches (one per tracked private company) + capped
// XML enrichment, ~2 minutes. Runs as its own fetch, independent of the 3-bit
// finalize_load flow — so a slow or failed SPV scan leaves the core Form D /
// N-PORT / S-1 data untouched. Results are stored raw in spv_raw_ and re-joined
// to companies_ in attach_spv_activity() on every emit, which makes the join
// robust regardless of which fetch lands first.
void PreIpoService::run_spv_fetch() {
    QJsonObject base;
    base[QStringLiteral("spv_days_back")]       = 1825;
    base[QStringLiteral("spv_hits_per_target")] = 30;
    base[QStringLiteral("spv_parse_max")]       = 40;
    QPointer<PreIpoService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form_d_data.py"),
        {QStringLiteral("spv_deep"), cursor_payload(base, QStringLiteral("spv"))},
        [self](python::PythonResult result) {
            if (!self) return;
            if (result.success && !result.output.trimmed().isEmpty()) {
                const QString js = python::extract_json(result.output);
                const QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8());
                if (doc.isObject()) {
                    const QJsonObject o = doc.object();
                    if (o.value(QStringLiteral("unchanged")).toBool()) {
                        LOG_INFO("PreIpo", "SPV activity unchanged — no new SPV filings.");
                        return;
                    }
                    self->parse_spv_response(o);
                    self->save_cache(QStringLiteral("spv.json"), doc);
                    if (o.contains(QStringLiteral("cursor"))) {
                        self->cursors_[QStringLiteral("spv")] = o.value(QStringLiteral("cursor"));
                        self->save_cursors();
                    }
                    self->emit_summary();
                    return;
                }
            }
            LOG_WARN("PreIpo", "SPV deep scan failed (non-fatal): " + result.error.left(200));
        },
        /*stream*/ {}, /*timeout*/ 240'000);
}

void PreIpoService::parse_spv_response(const QJsonObject& root) {
    QVector<SpvActivity> out;
    for (const auto& v : root.value(QStringLiteral("spv_activity")).toArray()) {
        const auto o = v.toObject();
        SpvActivity s;
        s.underlying_id    = o[QStringLiteral("underlying_id")].toString();
        s.underlying_name  = o[QStringLiteral("underlying_name")].toString();
        s.sponsor          = o[QStringLiteral("sponsor")].toString();
        s.spv_name         = o[QStringLiteral("spv_name")].toString();
        s.cik              = o[QStringLiteral("cik")].toString();
        s.filed_date       = QDate::fromString(o[QStringLiteral("filed_date")].toString(), Qt::ISODate);
        s.amount_sold_m    = o[QStringLiteral("amount_sold_m")].toDouble();
        s.amount_offered_m = o[QStringLiteral("amount_offered_m")].toDouble();
        s.minimum_investment_usd = o[QStringLiteral("minimum_investment_usd")].toDouble();
        s.num_investors    = o[QStringLiteral("num_investors")].toInt();
        s.edgar_url        = o[QStringLiteral("edgar_url")].toString();
        if (!s.underlying_id.isEmpty()) out.append(s);
    }
    // Don't wipe a good prior result on an empty/failed response.
    if (!out.isEmpty()) spv_raw_ = out;
}

void PreIpoService::attach_spv_activity() {
    for (auto& c : companies_) c.spv_activity.clear();
    if (spv_raw_.isEmpty()) return;

    QHash<QString, int> by_any_id;
    for (int i = 0; i < companies_.size(); ++i) {
        by_any_id[companies_[i].id] = i;
        for (const auto& al : companies_[i].aliases) by_any_id[al] = i;
    }

    for (const auto& s : spv_raw_) {
        int idx = -1;
        if (auto it = by_any_id.find(s.underlying_id); it != by_any_id.end()) idx = *it;
        if (idx < 0) {
            // Target not in the Form D / N-PORT universe yet — stub it so the
            // SPV interest is still visible. Idempotent across emits: the stub
            // persists in companies_, so the next attach finds it here.
            PrivateCompany stub;
            stub.id          = s.underlying_id;
            stub.name        = s.underlying_name;
            stub.description = QStringLiteral("Tracked via secondary-market SPV filings");
            stub.ipo_status  = IpoStatus::Unknown;
            stub.tags        = tags_for_company(stub);
            by_any_id[stub.id] = companies_.size();
            companies_.append(stub);
            idx = companies_.size() - 1;
        }
        companies_[idx].spv_activity.append(s);
    }

    for (auto& c : companies_)
        std::sort(c.spv_activity.begin(), c.spv_activity.end(),
                  [](const SpvActivity& a, const SpvActivity& b) {
                      return a.filed_date > b.filed_date;
                  });
}

// ── Nasdaq IPO calendar ───────────────────────────────────────────────────────

// ── Finnhub augmentation ─────────────────────────────────────────────────────
//
// Runs the Finnhub IPO calendar + lockup calendar via FinnhubService and
// stores the results alongside (NOT merged into) the SEC pipeline. The
// existing SEC + Nasdaq path stays untouched — Finnhub is additive.
// Skipped entirely when FINNHUB_API_KEY isn't set: the service returns
// an empty result, finnhub_ipos_/finnhub_lockups_ stay empty, the
// PicksView/PipelineView render without Finnhub-sourced rows.
void PreIpoService::run_finnhub_calendar_fetch() {
    if (!services::finnhub::FinnhubService::instance().has_api_key()) {
        // No-op: the user hasn't configured a Finnhub key. UI's gated on
        // finnhub_ipos_.isEmpty() / .has_api_key(), so this is invisible.
        return;
    }
    const QDate from = QDate::currentDate().addMonths(-3);  // recently priced
    const QDate to   = QDate::currentDate().addMonths(+3);  // upcoming
    QPointer<PreIpoService> self = this;
    services::finnhub::FinnhubService::instance().subscribe_ipo_calendar(
        this, from, to,
        [self](const services::query::QueryStore::State& s) {
            if (!self) return;
            if (!s.error.isEmpty() || !s.data.isValid() || s.data.isNull())
                return;
            self->finnhub_ipos_ = s.data.value<QVector<services::finnhub::FinnhubIPO>>();
            self->emit_summary();
        });
    services::finnhub::FinnhubService::instance().subscribe_lockups(
        this, QDate::currentDate(), QDate::currentDate().addMonths(+6), {},
        [self](const services::query::QueryStore::State& s) {
            if (!self) return;
            if (!s.error.isEmpty() || !s.data.isValid() || s.data.isNull())
                return;
            self->finnhub_lockups_ = s.data.value<QVector<services::finnhub::FinnhubLockup>>();
            self->emit_summary();
        });
}

void PreIpoService::run_nasdaq_ipo_fetch(const QString& yyyymm) {
    const QString url =
        QString("https://api.nasdaq.com/api/ipo/calendar?date=%1").arg(yyyymm);

    QPointer<PreIpoService> self = this;
    HttpClient::instance().get(url, [self](Result<QJsonDocument> res) {
        if (!self || !res.is_ok() || !res.value().isObject())
            return;

        const auto data = res.value().object()["data"].toObject();
        const QDate today = QDate::currentDate();

        // Returns "2026/05/15" format
        auto parse_nasdaq_date = [](const QString& s) -> QDate {
            return QDate::fromString(s.left(10), "yyyy/MM/dd");
        };

        // Strip common legal suffixes and punctuation for fuzzy name matching.
        // Plain toLower() misses "Cerebras Systems Inc" ↔ "Cerebras Systems, Inc."
        auto normalise = [](const QString& s) -> QString {
            QString r = s.toLower().trimmed();
            static const QStringList kSuffix = {
                ", inc.", " inc.", ", inc",  " inc",
                ", corp.", " corp.", ", corp", " corp",
                ", ltd.", " ltd.",  ", ltd",  " ltd",
                ", llc.", " llc.",  ", llc",
                ", co.", " co.",    ", co",
                ", plc", " plc",
            };
            for (const auto& suf : kSuffix) {
                if (r.endsWith(suf, Qt::CaseInsensitive)) { r.chop(suf.size()); break; }
            }
            r.remove(u','); r.remove(u'.'); r.remove(u'('); r.remove(u')');
            return r.simplified();
        };

        // Build a lookup: normalised company name → index in pipeline_
        QHash<QString, int> name_index;
        for (int i = 0; i < self->pipeline_.size(); ++i) {
            name_index[normalise(self->pipeline_[i].company_name)] = i;
        }

        auto merge_or_add = [&](const QString& company, const QString& ticker,
                                 const QDate& date, const QString& price_range) {
            if (company.isEmpty() || !date.isValid()) return;
            const QString key = normalise(company);
            if (name_index.contains(key)) {
                // Enrich existing EDGAR entry with confirmed date
                auto& entry = self->pipeline_[name_index[key]];
                entry.actual_ipo_date = date;
                entry.has_actual_date = true;
                if (!ticker.isEmpty()) entry.ticker = ticker;
                if (!price_range.isEmpty()) entry.price_range = price_range;
            } else {
                // Company not in EDGAR pipeline — add as a Nasdaq-sourced entry
                S1Filing f;
                f.company_name    = company;
                f.ticker          = ticker;
                f.filed_date      = today;     // use today as proxy for sorting
                f.amendment_count = 0;
                f.actual_ipo_date = date;
                f.has_actual_date = true;
                f.price_range     = price_range;
                self->pipeline_.append(f);
            }
        };

        // Upcoming
        for (const auto& v :
             data["upcoming"].toObject()["upcomingTable"].toObject()["rows"].toArray()) {
            const auto e = v.toObject();
            merge_or_add(e["companyName"].toString(),
                         e["proposedTickerSymbol"].toString(),
                         parse_nasdaq_date(e["expectedPriceDate"].toString()),
                         e["proposedSharePrice"].toString());
        }

        // Priced (recently listed — still useful for the record)
        for (const auto& v :
             data["priced"].toObject()["pricedTable"].toObject()["rows"].toArray()) {
            const auto e = v.toObject();
            merge_or_add(e["companyName"].toString(),
                         e["proposedTickerSymbol"].toString(),
                         parse_nasdaq_date(e["ipoDate"].toString()),
                         e["dealPrice"].toString());
        }

        LOG_INFO("PreIpo", "Nasdaq IPO calendar merged into pipeline");
        self->emit_summary();
    });
}

// ── Parsers ──────────────────────────────────────────────────────────────────

void PreIpoService::parse_form_d_response(const QJsonObject& root) {
    QHash<QString, int> by_id;
    for (int i = 0; i < companies_.size(); ++i) by_id[companies_[i].id] = i;

    // form_d_companies → PrivateCompany universe
    for (const auto& v : root.value(QStringLiteral("form_d_companies")).toArray()) {
        const auto o = v.toObject();
        PrivateCompany c;
        c.id          = o[QStringLiteral("id")].toString();
        c.cik         = o[QStringLiteral("cik")].toString();
        c.name        = o[QStringLiteral("name")].toString();
        c.sector      = sector_for_industry(o[QStringLiteral("industry_group")].toString());
        c.sub_sector  = o[QStringLiteral("industry_group")].toString();
        c.hq_city     = o[QStringLiteral("city")].toString();
        c.hq_state    = o[QStringLiteral("state")].toString();
        c.hq_country  = "USA";
        const QString yoi = o[QStringLiteral("year_of_incorporation")].toString();
        bool ok = false;
        const int yr = yoi.toInt(&ok);
        if (ok && yr > 1900 && yr < 2100) c.founded = QDate(yr, 1, 1);
        c.cumulative_raised_m = o[QStringLiteral("cumulative_raised_m")].toDouble();
        c.description = QStringLiteral("Source: SEC EDGAR Form D");
        c.ipo_status  = IpoStatus::Unknown;

        for (const auto& rv : o[QStringLiteral("rounds")].toArray()) {
            const auto ro = rv.toObject();
            PrimaryRound r;
            r.accession      = ro[QStringLiteral("adsh")].toString();
            r.filed_date     = QDate::fromString(ro[QStringLiteral("filed_date")].toString(), Qt::ISODate);
            r.first_sale_date = QDate::fromString(ro[QStringLiteral("first_sale_date")].toString(), Qt::ISODate);
            r.amount_sold_m  = ro[QStringLiteral("amount_m")].toDouble();
            r.amount_offered_m = ro[QStringLiteral("offering_m")].toDouble();
            r.exemption      = ro[QStringLiteral("exemption")].toString();
            for (const auto& t : ro[QStringLiteral("securities_types")].toArray())
                r.securities_types << t.toString();
            r.minimum_investment_usd = ro[QStringLiteral("minimum_investment_usd")].toDouble();
            for (const auto& pv : ro[QStringLiteral("related_persons")].toArray()) {
                const auto po = pv.toObject();
                RelatedPerson rp;
                rp.name = po[QStringLiteral("name")].toString();
                for (const auto& rl : po[QStringLiteral("roles")].toArray()) rp.roles << rl.toString();
                r.related_persons << rp;
            }
            r.edgar_url = ro[QStringLiteral("edgar_url")].toString();
            c.rounds.append(r);
        }
        if (!c.rounds.isEmpty()) {
            // newest first by filed_date
            std::sort(c.rounds.begin(), c.rounds.end(),
                      [](const PrimaryRound& a, const PrimaryRound& b) {
                          return a.filed_date > b.filed_date;
                      });
            c.last_round_date = c.rounds.first().filed_date;
            c.last_round_name = c.rounds.first().round_name_inferred.isEmpty()
                ? QStringLiteral("Form D (%1)").arg(c.last_round_date.toString("MMM yyyy"))
                : c.rounds.first().round_name_inferred;
        }
        c.tags = tags_for_company(c);

        if (auto it = by_id.find(c.id); it != by_id.end()) {
            // Preserve fund_marks / s1 / watched that may have arrived earlier
            const auto& existing = companies_[*it];
            c.watched        = existing.watched;
            c.fund_marks     = existing.fund_marks;
            // spv_activity is intentionally NOT preserved here: it's a transient
            // projection of spv_raw_, rebuilt for every company by
            // attach_spv_activity() at the top of each emit_summary().
            c.secondary      = existing.secondary;
            c.s1             = existing.s1;
            c.fin            = existing.fin;
            companies_[*it]  = c;
        } else {
            by_id[c.id] = companies_.size();
            companies_.append(c);
        }
    }

    // recent_form_d → flat ticker feed
    form_d_.clear();
    for (const auto& v : root.value(QStringLiteral("recent_form_d")).toArray()) {
        const auto o = v.toObject();
        FormDFiling f;
        f.company_name  = o[QStringLiteral("company_name")].toString();
        f.cik           = o[QStringLiteral("cik")].toString();
        f.filed_date    = QDate::fromString(o[QStringLiteral("filed_date")].toString(), Qt::ISODate);
        f.amount_raised = o[QStringLiteral("amount_raised")].toDouble();
        f.exemption     = o[QStringLiteral("exemption")].toString();
        f.offering_type = o[QStringLiteral("offering_type")].toString();
        f.state         = o[QStringLiteral("state")].toString();
        f.edgar_url     = o[QStringLiteral("edgar_url")].toString();
        if (!f.company_name.isEmpty()) form_d_.append(f);
    }

    // Note: the Form D Python script also returns a lightweight `s1_filings`
    // array, but we DON'T populate pipeline_ here. The dedicated S-1 fetcher
    // (sec_s1_pipeline.py) returns richer data (form types, amendment dates,
    // days-since-first-filed) and lands in parse_pipeline_response. If
    // Form D's callback runs after the S-1 callback, we'd clobber the rich
    // pipeline_ with thin rows. Skip it entirely — S-1 is the source of
    // truth for the pipeline.
}

void PreIpoService::parse_marks_response(const QJsonObject& root) {
    // Three lookup indices into companies_, in priority order:
    //   1. CIK (most reliable — only set when both sides have a CIK)
    //   2. canonical id (e.g. "stripe") — set by Form D slug AND alias
    //   3. case-folded name match (last resort)
    QHash<QString, int> by_cik, by_id;
    for (int i = 0; i < companies_.size(); ++i) {
        if (!companies_[i].cik.isEmpty()) by_cik[companies_[i].cik] = i;
        by_id[companies_[i].id] = i;
    }

    // Clear existing marks only if the new response actually contains marks
    // for at least one company — a degenerate response (network glitch
    // returning {marks:{}, aliases:{...}}) would otherwise silently wipe
    // every company's existing fund-mark data.
    const auto marks_root = root.value(QStringLiteral("marks")).toObject();
    if (!marks_root.isEmpty()) {
        for (auto& c : companies_) c.fund_marks.clear();
    }

    // Alias map (cid → {name, cik, aliases}) — lets us reverse-look up a
    // company entry by alias name or canonical CIK.
    const auto aliases_obj = root.value(QStringLiteral("aliases")).toObject();

    // Add aliases to existing Form D entries we recognize. After this pass,
    // future lookups by cid hit the Form D entry instead of creating a stub.
    for (auto it = aliases_obj.begin(); it != aliases_obj.end(); ++it) {
        const QString cid = it.key();
        const auto o = it.value().toObject();
        const QString alias_cik = o[QStringLiteral("cik")].toString();
        const QString canonical = o[QStringLiteral("name")].toString();

        int idx = -1;
        if (!alias_cik.isEmpty()) {
            if (auto fc = by_cik.find(alias_cik); fc != by_cik.end()) idx = *fc;
        }
        if (idx < 0) {
            if (auto fi = by_id.find(cid); fi != by_id.end()) idx = *fi;
        }
        if (idx < 0 && !canonical.isEmpty()) {
            const QString cf = canonical.toLower();
            for (int i = 0; i < companies_.size(); ++i)
                if (companies_[i].name.toLower() == cf) { idx = i; break; }
        }
        if (idx >= 0) {
            auto& c = companies_[idx];
            // Mirror the canonical id into the company's aliases so future
            // lookups by cid resolve.
            if (!c.aliases.contains(cid)) c.aliases.append(cid);
            by_id[cid] = idx;  // keep the index map consistent
        }
    }

    for (auto it = marks_root.begin(); it != marks_root.end(); ++it) {
        const QString cid = it.key();
        const auto arr = it.value().toArray();
        if (arr.isEmpty()) continue;

        const auto first_mark = arr.first().toObject();
        const QString mark_cik = first_mark[QStringLiteral("company_cik")].toString();
        const QString canonical = first_mark[QStringLiteral("company_name")].toString();

        // Resolve to an existing company entry by CIK, then alias-cid, then
        // fall back to creating a stub.
        int idx = -1;
        if (!mark_cik.isEmpty()) {
            if (auto fc = by_cik.find(mark_cik); fc != by_cik.end()) idx = *fc;
        }
        if (idx < 0) {
            if (auto fi = by_id.find(cid); fi != by_id.end()) idx = *fi;
        }
        if (idx < 0) {
            PrivateCompany c;
            c.id   = cid;
            c.cik  = mark_cik;
            // Prefer the canonical alias name over slugified cid; fall back
            // to a humanized cid if neither is available.
            c.name = !canonical.isEmpty() ? canonical
                                          : cid.left(1).toUpper() + cid.mid(1).replace('-', ' ');
            c.aliases.append(cid);
            c.description = QStringLiteral("Tracked via mutual-fund N-PORT marks");
            c.ipo_status  = IpoStatus::Unknown;
            c.tags = tags_for_company(c);
            by_id[cid] = companies_.size();
            if (!c.cik.isEmpty()) by_cik[c.cik] = companies_.size();
            companies_.append(c);
            idx = companies_.size() - 1;
        }

        QVector<FundMark>& marks = companies_[idx].fund_marks;
        for (const auto& mv : arr) {
            const auto mo = mv.toObject();
            FundMark m;
            m.fund_name      = mo[QStringLiteral("fund_name")].toString();
            m.fund_cik       = mo[QStringLiteral("fund_cik")].toString();
            m.issuer_raw     = mo[QStringLiteral("issuer_raw")].toString();
            m.as_of          = QDate::fromString(mo[QStringLiteral("as_of")].toString(), Qt::ISODate);
            m.filed_date     = QDate::fromString(mo[QStringLiteral("filed_date")].toString(), Qt::ISODate);
            m.shares_held    = mo[QStringLiteral("shares_held")].toDouble();
            m.fair_value_usd = mo[QStringLiteral("fair_value_usd")].toDouble();
            m.mark_pps       = mo[QStringLiteral("mark_pps")].toDouble();
            marks.append(m);
        }
        std::sort(marks.begin(), marks.end(),
                  [](const FundMark& a, const FundMark& b) { return a.as_of > b.as_of; });
    }

    funds_.clear();
    for (const auto& v : root.value(QStringLiteral("fund_families")).toArray()) {
        const auto o = v.toObject();
        FundEntry f;
        f.label = o[QStringLiteral("label")].toString();
        f.cik   = o[QStringLiteral("cik")].toString();
        funds_.append(f);
    }
}

void PreIpoService::parse_pipeline_response(const QJsonArray& arr) {
    QHash<QString, int> by_cik;
    for (int i = 0; i < companies_.size(); ++i)
        if (!companies_[i].cik.isEmpty()) by_cik[companies_[i].cik] = i;

    pipeline_.clear();
    for (const auto& v : arr) {
        const auto o = v.toObject();
        S1Filing s;
        s.company_name    = o[QStringLiteral("company_name")].toString();
        s.cik             = o[QStringLiteral("cik")].toString();
        s.filed_date      = QDate::fromString(o[QStringLiteral("first_filed")].toString(), Qt::ISODate);
        s.amendment_count = o[QStringLiteral("amendment_count")].toInt();
        s.is_amendment    = s.amendment_count > 0;
        s.edgar_url       = o[QStringLiteral("edgar_url")].toString();
        if (!s.company_name.isEmpty()) pipeline_.append(s);

        int idx = -1;
        if (auto it = by_cik.find(s.cik); it != by_cik.end()) {
            idx = *it;
        } else if (!s.cik.isEmpty()) {
            // S-1 filers usually aren't tracked elsewhere (Form D / N-PORT)
            // so we create a stub in the universe. Without this, clicking
            // a pipeline card produces no Markets dossier because the
            // PipelineView's cik→id lookup misses.
            PrivateCompany stub;
            stub.cik    = s.cik;
            stub.name   = s.company_name;
            // Slugify the name for the id. Keep simple — same regex as the
            // Python form-d script's _slug.
            static const QRegularExpression non_alnum("[^a-z0-9]+");
            QString slug = s.company_name.toLower();
            slug.replace(non_alnum, "-");
            while (slug.endsWith('-')) slug.chop(1);
            while (slug.startsWith('-')) slug.remove(0, 1);
            stub.id = slug.isEmpty() ? s.cik : slug;
            stub.description = QStringLiteral("Tracked via SEC S-1 / F-1 filings");
            stub.ipo_status  = IpoStatus::Filed;
            stub.tags = tags_for_company(stub);
            by_cik[stub.cik] = companies_.size();
            companies_.append(stub);
            idx = companies_.size() - 1;
        }

        if (idx >= 0) {
            auto& c = companies_[idx];
            c.s1.first_filed     = s.filed_date;
            c.s1.amendment_count = s.amendment_count;
            c.s1.latest_amended  = QDate::fromString(o[QStringLiteral("latest_amended")].toString(), Qt::ISODate);
            c.s1.days_since_first_filed = o[QStringLiteral("days_since_first")].toInt();
            c.s1.edgar_url       = s.edgar_url;
            c.s1.status_label    = s.amendment_count > 0 ? "Amended" : "Filed";
            c.s1.form_types.clear();
            for (const auto& ft : o[QStringLiteral("form_types")].toArray())
                c.s1.form_types << ft.toString();
            c.ipo_status   = IpoStatus::Filed;
            c.s1_filed_date = s.filed_date;
        }
    }
}

// ── Analytics ────────────────────────────────────────────────────────────────

void PreIpoService::recompute_analytics() {
    signals_.clear();
    const QDateTime now = QDateTime::currentDateTime();

    for (auto& c : companies_) {
        Analytics a{};

        // Consensus mark + dispersion (latest as_of within ~120 days of newest).
        if (!c.fund_marks.isEmpty()) {
            const QDate newest = c.fund_marks.first().as_of;
            QVector<double> latest;
            double sum_w = 0, sum_wx = 0;
            for (const auto& m : c.fund_marks) {
                if (m.mark_pps <= 0) continue;
                if (newest.isValid() && m.as_of.daysTo(newest) > 120) continue;
                const double w = std::max(m.shares_held, 1.0);
                sum_w  += w;
                sum_wx += w * m.mark_pps;
                latest.append(m.mark_pps);
            }
            if (sum_w > 0) a.consensus_mark_pps = sum_wx / sum_w;
            if (latest.size() >= 2 && a.consensus_mark_pps > 0) {
                double var = 0;
                for (double v : latest) { const double d = v - a.consensus_mark_pps; var += d * d; }
                const double sd = std::sqrt(var / latest.size());
                a.mark_dispersion_pct = (sd / a.consensus_mark_pps) * 100.0;
            }
            a.smart_money_index = static_cast<int>(latest.size());
        }

        // Mark drift: "consensus today vs consensus one quarter back". Form D
        // doesn't disclose price-per-share, so we use fund-mark history as
        // proxy. The baseline must be at least 45 days older than the newest
        // mark; otherwise comparing marks 2 weeks apart and labelling the
        // delta "vs prior quarter" is misleading. If there's no 45-day-old
        // mark, drift stays 0 and no signal fires.
        if (a.consensus_mark_pps > 0 && c.fund_marks.size() >= 2) {
            const QDate newest_as_of = c.fund_marks.first().as_of;
            double baseline = 0;
            for (const auto& m : c.fund_marks) {
                if (m.mark_pps <= 0) continue;
                // newest_as_of.daysTo(m.as_of) is negative when m is older;
                // ≤ -45 means m is at least 45 days older than the newest.
                if (newest_as_of.daysTo(m.as_of) <= -45) {
                    baseline = m.mark_pps;
                    break;
                }
            }
            if (baseline > 0 && std::abs(baseline - a.consensus_mark_pps) > 1e-9) {
                a.mark_drift_vs_last_round_pct =
                    (a.consensus_mark_pps - baseline) / baseline * 100.0;
            }
            // No 45-day-stale baseline: leave drift as 0. The picks/signals
            // engine then won't fire a MarkUp signal off a 2-week wobble.
        }

        // Hiive premium vs consensus mark.
        if (!c.secondary.isEmpty() && a.consensus_mark_pps > 0) {
            const double last = c.secondary.first().last_pps;
            if (last > 0)
                a.hiive_premium_pct = (last - a.consensus_mark_pps) / a.consensus_mark_pps * 100.0;
        }

        // IPO readiness score (0-100).
        int score = 0;
        if (c.s1.first_filed.isValid()) score += 30;
        if (c.s1.amendment_count >= 1)  score += 15;
        if (c.fin.revenue_m >= 100)     score += 20;
        if (c.founded.isValid() && c.founded.daysTo(QDate::currentDate()) >= 7 * 365) score += 10;
        if (c.cumulative_raised_m >= 500) score += 10;
        // sector multiple favorable proxy: tech / fintech / ai get +15
        if (c.tags.contains("ai") || c.tags.contains("fintech") || c.sector == "Technology") score += 15;
        a.ipo_readiness_score = std::min(100, score);

        // Days-to-price estimate: median 90 days for tech S-1 historically.
        if (c.s1.first_filed.isValid())
            a.days_to_price_est = std::max(0, 90 - c.s1.days_since_first_filed);

        // Composite picks score: weighted blend (rough, sign-adjusted).
        double cp = 0;
        cp += 0.4 * a.ipo_readiness_score;                 // up to 40
        cp += 0.15 * std::min(50.0, std::abs(a.mark_drift_vs_last_round_pct));
        cp += 0.10 * std::min(50.0, a.consensus_mark_pps > 0 ? 50.0 : 0.0);
        cp += 0.10 * std::min(40, a.smart_money_index * 8);
        cp -= 0.10 * std::min(50.0, a.mark_dispersion_pct);
        a.composite_picks_score = cp;

        // Sync back-compat fields the legacy detail panel still reads.
        // Form D doesn't disclose price-per-share or shares outstanding, so we
        // can't derive a company valuation from primary rounds alone. The
        // dossier panel shows cumulative-raised (KPI band) and the consensus
        // mark ($/share) instead.
        c.last_valuation_usd = 0;
        c.revenue_est_usd = c.fin.revenue_m;
        c.s1_filed_date   = c.s1.first_filed;
        if (c.s1.first_filed.isValid())
            c.ipo_expected_window = QString("S-1 filed %1").arg(c.s1.first_filed.toString("MMM yyyy"));

        c.analytics = a;

        // Generate signals. Timestamp each from the underlying event (latest
        // mark as_of, latest amendment date, latest S-1 filing) so the
        // chronological sort downstream is meaningful — every signal would
        // otherwise share the same `now` and the sort would be a no-op.
        if (a.consensus_mark_pps > 0 && a.mark_drift_vs_last_round_pct > 10) {
            Signal s; s.company_id = c.id; s.company_name = c.name;
            s.kind = SignalKind::MarkUp;
            s.description = QString("Consensus mark +%1% vs prior quarter")
                                .arg(a.mark_drift_vs_last_round_pct, 0, 'f', 1);
            s.at = c.fund_marks.isEmpty()
                ? now
                : QDateTime(c.fund_marks.first().as_of, QTime(16, 0));
            signals_.append(s);
        }
        if (c.s1.amendment_count >= 3) {
            Signal s; s.company_id = c.id; s.company_name = c.name;
            s.kind = SignalKind::AmendmentBurst;
            s.description = QString("%1 S-1 amendments — pricing imminent").arg(c.s1.amendment_count);
            s.at = c.s1.latest_amended.isValid()
                ? QDateTime(c.s1.latest_amended, QTime(16, 0))
                : now;
            signals_.append(s);
        }
        if (a.ipo_readiness_score >= 70) {
            Signal s; s.company_id = c.id; s.company_name = c.name;
            s.kind = SignalKind::ReadinessJump;
            s.description = QString("IPO readiness %1/100").arg(a.ipo_readiness_score);
            s.at = c.s1.first_filed.isValid()
                ? QDateTime(c.s1.first_filed, QTime(16, 0))
                : now;
            signals_.append(s);
        }
    }

    // Sort universe by composite picks score desc so left-rail prioritizes interesting names.
    std::sort(companies_.begin(), companies_.end(),
              [](const PrivateCompany& a, const PrivateCompany& b) {
                  if (a.analytics.composite_picks_score != b.analytics.composite_picks_score)
                      return a.analytics.composite_picks_score > b.analytics.composite_picks_score;
                  return a.cumulative_raised_m > b.cumulative_raised_m;
              });

    std::sort(signals_.begin(), signals_.end(),
              [](const Signal& a, const Signal& b) { return a.at > b.at; });
}

// ── Emit ─────────────────────────────────────────────────────────────────────

// ── Curated valuation seed ─────────────────────────────────────────────────────

void PreIpoService::load_valuation_seed() {
    if (seed_loaded_) return;
    seed_loaded_ = true;  // attempt once per session regardless of outcome

    // Parse one seed file's `entries` array into ValuationSeed objects.
    auto parse_file = [](const QString& path, const char* label) -> QVector<ValuationSeed> {
        QVector<ValuationSeed> out;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return out;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN("PreIpo", QString("valuation seed %1 parse failed: %2")
                                   .arg(label, err.errorString()));
            return out;
        }
        for (const auto& v : doc.object().value(QStringLiteral("entries")).toArray()) {
            const auto o = v.toObject();
            ValuationSeed s;
            s.id = o[QStringLiteral("id")].toString();
            if (s.id.isEmpty()) continue;
            s.name = o[QStringLiteral("name")].toString();
            s.cik  = o[QStringLiteral("cik")].toString();
            for (const auto& a : o[QStringLiteral("aliases")].toArray()) s.aliases << a.toString();
            s.last_valuation_usd = o[QStringLiteral("last_valuation_usd")].toDouble();
            s.as_of        = QDate::fromString(o[QStringLiteral("as_of")].toString(), Qt::ISODate);
            s.source_label = o[QStringLiteral("source_label")].toString();
            s.source_url   = o[QStringLiteral("source_url")].toString();
            s.round_name   = o[QStringLiteral("round_name")].toString();
            s.sector       = o[QStringLiteral("sector")].toString();
            s.description  = o[QStringLiteral("description")].toString();
            s.hq_city      = o[QStringLiteral("hq_city")].toString();
            s.hq_state     = o[QStringLiteral("hq_state")].toString();
            s.hq_country   = o[QStringLiteral("hq_country")].toString();
            s.founded_year = o[QStringLiteral("founded")].toInt();
            for (const auto& k : o[QStringLiteral("key_investors")].toArray()) s.key_investors << k.toString();
            for (const auto& t : o[QStringLiteral("tags")].toArray()) s.tags << t.toString();
            out.append(s);
        }
        return out;
    };

    // 1) Bundled seed (ships with the app, under scripts/).
    seed_ = parse_file(
        python::PythonRunner::instance().scripts_dir() + "/pre_ipo_valuation_seed.json", "bundled");

    // 2) User override (optional): FINCEPT_DATA_DIR/pre_ipo_valuation_seed.json,
    //    falling back to the app data dir. Lets the user refresh stale
    //    valuations or add private names WITHOUT a rebuild. Override entries
    //    upsert by id — same id replaces the bundled entry, new ids are added.
    QString data_dir = qEnvironmentVariable("FINCEPT_DATA_DIR");
    if (data_dir.isEmpty())
        data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QVector<ValuationSeed> overrides =
        parse_file(data_dir + "/pre_ipo_valuation_seed.json", "user override");
    int replaced = 0, added = 0;
    for (const auto& ov : overrides) {
        int idx = -1;
        for (int i = 0; i < seed_.size(); ++i)
            if (seed_[i].id == ov.id) { idx = i; break; }
        if (idx >= 0) { seed_[idx] = ov; ++replaced; }
        else          { seed_.append(ov); ++added; }
    }

    LOG_INFO("PreIpo", QString("loaded %1 curated valuation seeds (overrides: %2 replaced, %3 added)")
                           .arg(seed_.size()).arg(replaced).arg(added));
}

QSet<QString> PreIpoService::seed_fuzzy_norms(const ValuationSeed& s) const {
    QSet<QString> norms;
    const QString nm = norm_name(s.name);
    if (!nm.isEmpty()) norms.insert(nm);
    // Only multi-word aliases participate in fuzzy matching — a bare single
    // token (e.g. "figure") is too generic and would bind to an unrelated
    // company. Single-token aliases still match exactly via the id tier.
    for (const auto& a : s.aliases) {
        if (!a.contains(' ')) continue;
        const QString an = norm_name(a);
        if (!an.isEmpty()) norms.insert(an);
    }
    return norms;
}

bool PreIpoService::seed_matches(const PrivateCompany& c, const ValuationSeed& s,
                                 const QSet<QString>& norms) const {
    // 1. CIK (most reliable — set only for verified operating-company filers).
    if (!s.cik.isEmpty() && c.cik == s.cik) return true;
    // 2. Exact id/alias: the seed id, or any seed alias (slugified) matching the
    //    company id or one of its aliases. This is how single-token aliases
    //    (e.g. "scaleai") match — they're excluded from the fuzzy tier.
    if (c.id == s.id || c.aliases.contains(s.id)) return true;
    for (const auto& a : s.aliases) {
        const QString slug = slugify(a);
        if (c.aliases.contains(a)) return true;
        if (!slug.isEmpty() && (c.id == slug || c.aliases.contains(slug))) return true;
    }
    // 3. Fuzzy normalized-name match (canonical name + multi-word aliases only).
    if (norms.contains(norm_name(c.name))) return true;
    for (const auto& a : c.aliases)
        if (norms.contains(norm_name(a))) return true;
    return false;
}

int PreIpoService::find_company_for_seed(const ValuationSeed& s) const {
    const QSet<QString> norms = seed_fuzzy_norms(s);
    // Prefer a CIK match (most reliable) before any other tier, then fall back
    // to the shared predicate. seed_ensure_companies has normally already
    // merged fragments into one row by the time this runs, so there's a single
    // match — but the CIK-first pass keeps the choice deterministic regardless.
    if (!s.cik.isEmpty())
        for (int i = 0; i < companies_.size(); ++i)
            if (companies_[i].cik == s.cik) return i;
    for (int i = 0; i < companies_.size(); ++i)
        if (seed_matches(companies_[i], s, norms)) return i;
    return -1;
}

void PreIpoService::seed_ensure_companies() {
    if (seed_.isEmpty()) return;
    for (const auto& s : seed_) {
        // Fuzzy norm-set (canonical name + multi-word aliases) — shared with
        // find_company_for_seed so the two passes can't disagree on matching.
        const QSet<QString> norms = seed_fuzzy_norms(s);

        // Gather ALL matching rows so fragmented duplicates (canonical-slug
        // stub vs Form-D legal-name entry) get merged, not left side by side.
        // Uses the same predicate as find_company_for_seed so the two passes
        // can't disagree about what a seed maps to.
        QVector<int> matches;
        for (int i = 0; i < companies_.size(); ++i)
            if (seed_matches(companies_[i], s, norms)) matches.append(i);

        if (matches.isEmpty()) {
            // Seeded name with no SEC footprint (OpenAI, Anthropic, …): stub it
            // so marquee names always appear. Stubs are rebuilt from seed_ each
            // emit, so they survive cache hydration without being persisted.
            PrivateCompany stub;
            stub.id   = s.id;
            stub.cik  = s.cik;
            stub.name = s.name;
            stub.aliases = s.aliases;
            stub.sector  = s.sector;
            stub.hq_city = s.hq_city; stub.hq_state = s.hq_state; stub.hq_country = s.hq_country;
            if (s.founded_year > 1900 && s.founded_year < 2100) stub.founded = QDate(s.founded_year, 1, 1);
            stub.key_investors = s.key_investors;
            stub.description = s.description.isEmpty()
                ? QStringLiteral("Tracked via curated valuation seed") : s.description;
            stub.ipo_status = IpoStatus::Rumored;
            stub.tags = s.tags.isEmpty() ? tags_for_company(stub) : s.tags;
            companies_.append(stub);
            continue;
        }

        // Pick the richest match as primary: prefer one with a CIK, then the one
        // with the most Form D rounds.
        int primary = matches.front();
        for (int idx : matches) {
            const auto& c = companies_[idx];
            const auto& p = companies_[primary];
            const bool better =
                (!c.cik.isEmpty() && p.cik.isEmpty()) ||
                (c.cik.isEmpty() == p.cik.isEmpty() && c.rounds.size() > p.rounds.size());
            if (better) primary = idx;
        }
        QVector<int> remove;
        for (int idx : matches)
            if (idx != primary) { merge_company_into(companies_[primary], companies_[idx]); remove.append(idx); }

        auto& p = companies_[primary];
        auto add_alias = [&](const QString& a) {
            if (!a.isEmpty() && a != p.id && !p.aliases.contains(a)) p.aliases << a;
        };
        // Prefer the friendlier seed display name (e.g. "SpaceX" over the Form D
        // legal name); keep the legal name searchable as an alias.
        if (!s.name.isEmpty() && p.name != s.name) { add_alias(p.name); p.name = s.name; }
        add_alias(s.id);
        add_alias(slugify(s.name));
        for (const auto& a : s.aliases) add_alias(a);
        // Fill descriptive fields only where SEC data is absent.
        if (p.sector.isEmpty())  p.sector = s.sector;
        if (!p.founded.isValid() && s.founded_year > 1900 && s.founded_year < 2100)
            p.founded = QDate(s.founded_year, 1, 1);
        if (p.hq_city.isEmpty())    p.hq_city = s.hq_city;
        if (p.hq_state.isEmpty())   p.hq_state = s.hq_state;
        if (p.hq_country.isEmpty()) p.hq_country = s.hq_country;
        if (p.key_investors.isEmpty()) p.key_investors = s.key_investors;
        if (!s.description.isEmpty() &&
            (p.description.isEmpty() || p.description.startsWith("Tracked via") ||
             p.description.startsWith("Source: SEC")))
            p.description = s.description;

        std::sort(remove.begin(), remove.end(), std::greater<int>());
        for (int idx : remove) companies_.removeAt(idx);
    }
}

void PreIpoService::seed_apply_valuation() {
    for (const auto& s : seed_) {
        const int idx = find_company_for_seed(s);
        if (idx < 0) continue;
        auto& c = companies_[idx];
        c.last_valuation_usd = s.last_valuation_usd;  // overwrites the 0 set by recompute
        c.seed_source     = s.source_label;
        c.seed_source_url = s.source_url;
        c.seed_as_of      = s.as_of;
        c.seed_round      = s.round_name;
    }
}

void PreIpoService::emit_summary() {
    // Order matters: reconcile/stub seeded names BEFORE attach_spv_activity (so
    // SPVs join to the canonical entry via aliases instead of spawning a dup
    // stub), and apply valuations AFTER recompute_analytics (which zeroes
    // last_valuation_usd on every pass).
    seed_ensure_companies();
    attach_spv_activity();  // re-join the SPV side table to the latest companies_
    recompute_analytics();
    seed_apply_valuation();
    PreIpoSummary summary;
    summary.companies     = companies_;
    summary.recent_form_d = form_d_;
    summary.ipo_pipeline  = pipeline_;
    summary.signal_list   = signals_;
    summary.funds         = funds_;
    summary.last_updated  = QDateTime::currentDateTime();
    summary.loaded        = (pending_bits_ == 0 && failed_bits_ == 0);
    emit data_loaded(summary);
}

void PreIpoService::finalize_load() {
    loading_ = false;
    force_refresh_ = false;
    const bool any_failed = failed_bits_ != 0;
    loaded_ = !any_failed;
    LOG_INFO("PreIpo",
             QString("Load finished: %1 companies, %2 S-1 filers, %3 signals, "
                     "%4 funds (failed_bits=%5)")
                 .arg(companies_.size())
                 .arg(pipeline_.size())
                 .arg(signals_.size())
                 .arg(funds_.size())
                 .arg(failed_bits_));
    if (any_failed) {
        emit error_occurred(
            QStringLiteral("One or more SEC sources failed; partial data shown. "
                           "The screen will retry on next open."));
    }
}

// ── Daily auto-refresh ───────────────────────────────────────────────────────

void PreIpoService::schedule_next_daily_refresh() {
    const QTimeZone et("America/New_York");
    if (!et.isValid()) {
        LOG_WARN("PreIpo", "America/New_York timezone unavailable; daily auto-refresh disabled");
        return;
    }
    const QDateTime now_et = QDateTime::currentDateTime().toTimeZone(et);
    QDateTime target(now_et.date(), QTime(5, 0), et);
    if (target <= now_et) target = target.addDays(1);
    const qint64 ms = now_et.msecsTo(target);

    LOG_INFO("PreIpo",
             QString("Next daily auto-refresh at %1 ET (%2 min away)")
                 .arg(target.toString("yyyy-MM-dd hh:mm"))
                 .arg(ms / 60'000));

    QPointer<PreIpoService> self = this;
    QTimer::singleShot(static_cast<int>(std::min<qint64>(ms, INT_MAX)),
                       this, [self]() {
        if (!self) return;
        if (self->loading_) {
            LOG_INFO("PreIpo", "Daily auto-refresh fired while a load is in flight — skipping this tick");
        } else {
            LOG_INFO("PreIpo", "Daily auto-refresh firing (05:00 ET pre-warm)");
            self->refresh_internal(/*force*/ false);  // data-based: cheap if nothing filed
        }
        self->schedule_next_daily_refresh();
    });
}

// ── Persistent cache ─────────────────────────────────────────────────────────

QString PreIpoService::cache_dir() const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString dir = root + "/cache/pre_ipo";
    QDir().mkpath(dir);
    return dir;
}

bool PreIpoService::load_from_cache() {
    bool loaded_any = false;
    auto read = [&](const QString& filename) -> QJsonDocument {
        const QString path = cache_dir() + "/" + filename;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        const QByteArray bytes = f.readAll();
        f.close();
        return QJsonDocument::fromJson(bytes);
    };

    // Self-heal cursor/cache desync: a source's change cursor is only
    // trustworthy if that source's cache actually loaded. If the cache file is
    // missing or corrupt while its cursor persisted (e.g. a prior save_cache()
    // failed on a full disk, or the file was truncated), the data-based gate
    // would return "unchanged" against the live cursor forever and the data
    // would never reload. Drop the cursor for any source that didn't hydrate so
    // the next refresh fetches it in full (which rewrites both cache + cursor).
    bool cursors_changed = false;
    auto load_source = [&](const QString& file, const QString& cursor_key,
                           auto&& parse_if_ok) {
        const QJsonDocument doc = read(file);
        if (parse_if_ok(doc)) {
            loaded_any = true;
        } else if (cursors_.contains(cursor_key)) {
            cursors_.remove(cursor_key);
            cursors_changed = true;
        }
    };

    load_source(QStringLiteral("form_d.json"), QStringLiteral("form_d"),
                [&](const QJsonDocument& d) {
                    if (!d.isObject()) return false;
                    parse_form_d_response(d.object());
                    return true;
                });
    load_source(QStringLiteral("marks.json"), QStringLiteral("marks"),
                [&](const QJsonDocument& d) {
                    if (!d.isObject()) return false;
                    parse_marks_response(d.object());
                    return true;
                });
    load_source(QStringLiteral("pipeline.json"), QStringLiteral("s1"),
                [&](const QJsonDocument& d) {
                    if (!d.isArray()) return false;
                    parse_pipeline_response(d.array());
                    return true;
                });
    load_source(QStringLiteral("spv.json"), QStringLiteral("spv"),
                [&](const QJsonDocument& d) {
                    if (!d.isObject()) return false;
                    parse_spv_response(d.object());  // attached on the next emit_summary()
                    return true;
                });

    if (cursors_changed) save_cursors();

    if (loaded_any) {
        LOG_INFO("PreIpo",
                 QString("Hydrated %1 companies from cache (%2 S-1 filers, %3 funds)")
                     .arg(companies_.size())
                     .arg(pipeline_.size())
                     .arg(funds_.size()));
    }
    return loaded_any;
}

void PreIpoService::save_cache(const QString& filename, const QJsonDocument& doc) {
    const QString path = cache_dir() + "/" + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("PreIpo", "Failed to open cache file for writing: " + path);
        return;
    }
    f.write(doc.toJson(QJsonDocument::Compact));
    f.close();
}

// ── Data-based change cursors ─────────────────────────────────────────────────

void PreIpoService::load_cursors() {
    QFile f(cache_dir() + "/cursors.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    cursors_ = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
}

void PreIpoService::save_cursors() {
    QFile f(cache_dir() + "/cursors.json");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("PreIpo", "Failed to write cursors.json");
        return;
    }
    f.write(QJsonDocument(cursors_).toJson(QJsonDocument::Compact));
    f.close();
}

QString PreIpoService::cursor_payload(QJsonObject base, const QString& src) const {
    // A forced refresh deliberately omits the prior cursor so Python treats it
    // as a first run and re-fetches in full.
    if (!force_refresh_ && cursors_.contains(src))
        base[QStringLiteral("prev_cursor")] = cursors_.value(src);
    return QString::fromUtf8(QJsonDocument(base).toJson(QJsonDocument::Compact));
}

} // namespace fincept::services
