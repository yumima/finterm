# finterm — test suite (opt-in)
#
# Enable by configuring with -DFINCEPT_BUILD_TESTS=ON. Disabled by default so
# normal dev / release builds remain unchanged. Each test is a self-contained
# Qt Test executable that links the minimum subset of sources it needs
# (no linking against the full FinceptTerminal target — keeps tests fast).

# Inherits ${_fincept_qt_match} from the parent CMakeLists (exact pin under
# -DFINCEPT_QT_STRICT=ON, else a major.minor floor that accepts system Qt).
find_package(Qt6 ${_fincept_qt_match} REQUIRED COMPONENTS Gui Widgets Network Test)

enable_testing()

# ── DataHub tests ────────────────────────────────────────────────────────────
# Links against the datahub sources and the Logger (a direct dependency).

add_executable(test_datahub
    datahub/test_datahub.cpp
    ../src/datahub/DataHub.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_datahub PRIVATE
    ../src
)

target_link_libraries(test_datahub PRIVATE
    Qt6::Core
    Qt6::Gui   # DataHub.cpp includes <QGuiApplication>
    Qt6::Test
)

# Tests shouldn't need the app's unity build settings.
set_target_properties(test_datahub PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME datahub COMMAND test_datahub)

# ── EventBus tests ───────────────────────────────────────────────────────────
# Reentrancy + thread-safety regression suite. Locks in the invariants that
# (1) handlers can subscribe()/unsubscribe()/publish() during dispatch
# without crashing or skipping events, and (2) cross-thread publish is safe
# (MCP tools publish from a worker thread).

add_executable(test_event_bus
    core/test_event_bus.cpp
    ../src/core/events/EventBus.cpp
)

target_include_directories(test_event_bus PRIVATE
    ../src
)

target_link_libraries(test_event_bus PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_event_bus PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME event_bus COMMAND test_event_bus)

# ── LiveHlsProxy::trim_playlist tests ────────────────────────────────────────
# Locks in the playlist-trim correctness. The whole live-HLS fix
# (commit 59a0e513) depends on the trim producing a valid playlist with
# the right MEDIA-SEQUENCE, per-segment tags carried through, etc.
# Compiles only the LiveHlsProxy.cpp + its Logger dependency — the
# trim function is static and pure, so we don't need to instantiate
# any QNetwork/QTcpServer machinery to exercise it.

add_executable(test_live_hls_proxy
    services/video/test_live_hls_proxy.cpp
    ../src/services/video/LiveHlsProxy.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_live_hls_proxy PRIVATE
    ../src
)

target_link_libraries(test_live_hls_proxy PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

set_target_properties(test_live_hls_proxy PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME live_hls_proxy COMMAND test_live_hls_proxy)

# ── News feed two-pane layout tests ──────────────────────────────────────────
# Guards the refactor that replaced the parity-proxy 2-column feed (+ a detail
# pane summoned between the columns) with one headline list and a permanent
# reading pane. Widget-level, so it needs a QApplication — run under the
# offscreen platform plugin in CI. Links the three news feed widget sources
# plus theme/logging; it never touches NewsService, so no network or storage
# machinery is pulled in.

add_executable(test_news_feed_panel
    ui/test_news_feed_panel.cpp
    ../src/screens/news/NewsFeedPanel.cpp
    ../src/screens/news/NewsSidePanel.cpp
    ../src/screens/news/NewsFeedModel.cpp
    ../src/screens/news/NewsFeedDelegate.cpp
    ../src/ui/theme/Theme.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_news_feed_panel PRIVATE
    ../src
)

target_link_libraries(test_news_feed_panel PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network  # headers only — NewsService.h includes QNetworkAccessManager
    Qt6::Test
)

set_target_properties(test_news_feed_panel PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME news_feed_panel COMMAND test_news_feed_panel)
# Headless: no display in CI, and the test only asserts geometry/model wiring.
set_tests_properties(news_feed_panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── Earnings calendar maths tests ────────────────────────────────────────────
# parse_money() and growth_verdict() decide what the Earnings Calendar tile
# prints and what colour it prints it in. Both are pure functions in a header,
# so the test needs no widget, no network and no Python daemon.

add_executable(test_earnings_math
    screens/test_earnings_math.cpp
)

target_include_directories(test_earnings_math PRIVATE
    ../src
)

target_link_libraries(test_earnings_math PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_earnings_math PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME earnings_math COMMAND test_earnings_math)

# ── Extended-hours (AFT%) maths tests ────────────────────────────────────────
# quote_from_row() decides the sign and the base of the dashboard's AFT%
# column. It shipped inverted once — AAPL down 6.5% displayed as up 6.3% — so
# the fixtures are real daemon payloads rather than invented numbers. Pure
# JSON-in/struct-out, no widget and no daemon.

add_executable(test_extended_hours_math
    screens/test_extended_hours_math.cpp
)

target_include_directories(test_extended_hours_math PRIVATE
    ../src
)

target_link_libraries(test_extended_hours_math PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_extended_hours_math PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME extended_hours_math COMMAND test_extended_hours_math)

# ── Earnings signal tests ────────────────────────────────────────────────────
# The pre-earnings BUY/HOLD/SELL scorer. Pure struct-in/verdict-out — no
# network, no widgets — so its calibration and its missing-data behaviour can
# be pinned exactly.

add_executable(test_earnings_signal
    services/test_earnings_signal.cpp
    ../src/services/equity/EarningsSignal.cpp
)

target_include_directories(test_earnings_signal PRIVATE
    ../src
)

target_link_libraries(test_earnings_signal PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_earnings_signal PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME earnings_signal COMMAND test_earnings_signal)

# ── Technical rating tests ───────────────────────────────────────────────────
# The STRONG BUY … STRONG SELL read under ER Technicals. Same shape as the
# earnings scorer — struct in, verdict out, no network and no widgets — so the
# rules that used to invert the verdict (oscillator-only voting, unsigned ADX,
# MACD against zero instead of its signal line) stay pinned.

add_executable(test_technical_rating
    services/test_technical_rating.cpp
    ../src/services/equity/TechnicalRating.cpp
)

target_include_directories(test_technical_rating PRIVATE
    ../src
)

target_link_libraries(test_technical_rating PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_technical_rating PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME technical_rating COMMAND test_technical_rating)

# ── prune_scripts_junk smoke test ────────────────────────────────────────────
# Pure cmake -P script that builds a fixture tree, runs the real prune script,
# and asserts legitimate dirs survive while junk is gone. Guards against the
# GLOB_RECURSE/LIST_DIRECTORIES regression that previously wiped technicals/.
add_test(
    NAME prune_scripts_junk
    COMMAND ${CMAKE_COMMAND}
            -DPRUNE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/prune_scripts_junk.cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_prune_scripts_junk.cmake
)

# ── technicals series scorer (tool, not a test) ──────────────────────────────
# Walks the real TechnicalRating scorer over a whole history so the rating can
# be measured against forward returns rather than argued about. Built here
# because this is the only target that already links TechnicalRating.cpp on its
# own. Driven by scripts/factor_rating; see tools/technicals_series/.

add_executable(technicals_score_series
    ../tools/technicals_series/score_series.cpp
    ../src/services/equity/TechnicalRating.cpp
)
target_include_directories(technicals_score_series PRIVATE ../src)
target_link_libraries(technicals_score_series PRIVATE Qt6::Core)
set_target_properties(technicals_score_series PROPERTIES UNITY_BUILD OFF AUTOMOC OFF)

# ── Portfolio snapshot provenance tests ──────────────────────────────────────
# Runs the real migrations against a temp SQLite file and pins the invariant
# that backfill (synthetic, current-quantity back-projection) can never
# overwrite a 'live' snapshot (a real valuation). The guard is SQL — an
# ON CONFLICT ... WHERE source <> 'live' upsert — so it is exercised through
# the actual Database/MigrationRunner/PortfolioRepository stack.

find_package(Qt6 ${_fincept_qt_match} REQUIRED COMPONENTS Sql)

add_executable(test_portfolio_snapshots
    storage/test_portfolio_snapshots.cpp
    ../src/storage/sqlite/Database.cpp
    ../src/services/portfolio/PortfolioLedger.cpp
    ../src/storage/sqlite/migrations/MigrationRunner.cpp
    ../src/storage/sqlite/migrations/v006_portfolio_multi.cpp
    ../src/storage/sqlite/migrations/v048_snapshot_source.cpp
    ../src/storage/sqlite/migrations/v049_opening_balances.cpp
    ../src/storage/repositories/PortfolioRepository.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_portfolio_snapshots PRIVATE
    ../src
)

target_link_libraries(test_portfolio_snapshots PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)

set_target_properties(test_portfolio_snapshots PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME portfolio_snapshots COMMAND test_portfolio_snapshots)

# ── Cache age derivation ─────────────────────────────────────────────────────
# "How old is this value" must come from the cache row, not from a field a
# writer has to remember to stamp. A forgotten stamp silently disabled quote
# staleness gating; these pin that a rewrite cannot un-age an entry.
add_executable(test_cache_aged
    storage/test_cache_aged.cpp
    ../src/storage/cache/CacheManager.cpp
    ../src/storage/sqlite/CacheDatabase.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_cache_aged PRIVATE
    ../src
)

target_link_libraries(test_cache_aged PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)

set_target_properties(test_cache_aged PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME cache_aged COMMAND test_cache_aged)

# ── Portfolio ledger (cost-basis engine) tests ───────────────────────────────
# The single average-cost convention every position number derives from.
# Pure replay function — struct in, position out — same shape as the
# TechnicalRating and EarningsSignal suites.

add_executable(test_portfolio_ledger
    services/test_portfolio_ledger.cpp
    ../src/services/portfolio/PortfolioLedger.cpp
)

target_include_directories(test_portfolio_ledger PRIVATE
    ../src
)

target_link_libraries(test_portfolio_ledger PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_portfolio_ledger PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME portfolio_ledger COMMAND test_portfolio_ledger)

# ── Portfolio time-weighted return tests ─────────────────────────────────────
# The period-return convention: deposits are flows, not performance. Pure
# chaining function over the snapshot series + transaction log.

add_executable(test_portfolio_returns
    services/test_portfolio_returns.cpp
    ../src/services/portfolio/PortfolioReturns.cpp
    ../src/services/portfolio/PortfolioFx.cpp
)

target_include_directories(test_portfolio_returns PRIVATE
    ../src
)

target_link_libraries(test_portfolio_returns PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_portfolio_returns PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME portfolio_returns COMMAND test_portfolio_returns)

# ── Portfolio FX convention tests ────────────────────────────────────────────
# London pence (GBp = 1/100 GBP) and the FX pair naming whose fetch key must
# equal its lookup key — a drift there silently reverts every cross-currency
# total to face value.

add_executable(test_portfolio_fx
    services/test_portfolio_fx.cpp
    ../src/services/portfolio/PortfolioFx.cpp
)

target_include_directories(test_portfolio_fx PRIVATE
    ../src
)

target_link_libraries(test_portfolio_fx PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_portfolio_fx PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME portfolio_fx COMMAND test_portfolio_fx)

# ── TALIpp indicator correctness ─────────────────────────────────────────────
# Pure-Python indicator tests. RSI and MFI returned the INVERSE of the truth
# for windows with no downside (0 = maximally oversold, for the strongest
# possible uptrend), and Stochastic %D overwrote a genuine 0.0 %K with a
# neutral 50. Self-contained; no pytest, no network.

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME equity_talipp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_equity_talipp.py
    )
endif()

# ── QueryStore provenance / revalidate tests ─────────────────────────────────
# The contract every ER panel depends on: data carries its upstream fetch
# time, and a periodic refresh (revalidate) never blanks a panel that is
# showing good numbers — including when the refresh itself fails.

add_executable(test_query_store
    services/test_query_store.cpp
    ../src/services/query/QueryStore.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_query_store PRIVATE
    ../src
)

target_link_libraries(test_query_store PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

set_target_properties(test_query_store PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME query_store COMMAND test_query_store)

# ── Daemon dispatch: single-flight ───────────────────────────────────────────
# N concurrent misses of the same key used to make N upstream calls, because
# the cache is checked before the call and populated after it. That duplicate
# load is what pushes the account into 429 rate limiting, which a user
# experiences as missing data rather than as slowness.

if(Python3_Interpreter_FOUND)
    add_test(
        NAME daemon_singleflight
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_daemon_singleflight.py
    )
    # 77 = "the app venv (with yfinance) isn't present" — reported as
    # skipped rather than passed, so a missing dependency can never mask a
    # regression.
    set_tests_properties(daemon_singleflight PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# ── Extended hours (AFT%) ────────────────────────────────────────────────────
# yfinance.download() keeps its results in module globals, so two overlapping
# downloads in the daemon returned mislabelled frames — one stock's bars under
# another stock's ticker, which put a foreign after-hours move on the MSFT row.
# Also pins the denominator (official close, not the 15:59 bar) and the refusal
# to price an extended print against a reference bar from an earlier day.

if(Python3_Interpreter_FOUND)
    add_test(
        NAME extended_hours
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_extended_hours.py
    )
    set_tests_properties(extended_hours PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# ── An earnings reaction must come from a session that has closed ────────────
# yfinance returns a bar for the CURRENT session as soon as it opens, with a
# running close. Read as p_after that turned a mid-morning quote into a
# finished "1-day earnings reaction" — and the signal ledger settles a print
# against it PERMANENTLY, since resolve() only touches unresolved rows. Pins
# the split: reaction_pct only once the session closes, reaction_live_pct until
# then, and settled history untouched by either.

if(Python3_Interpreter_FOUND)
    add_test(
        NAME earnings_reaction_finality
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_earnings_reaction_finality.py
    )
    set_tests_properties(earnings_reaction_finality
                         PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# ── Swap slippage: displayed vs sent ─────────────────────────────────────────
# A configured tolerance (basis points) and an effective one (integer percent,
# what the transaction carries) are different numbers, and the UI quoted the
# first while sending the second — including on the confirm dialog, whose
# warning named the figure it showed as the bound the trade would be rejected
# past. 0.25% shown, 1% sent, before an irreversible on-chain swap.
add_executable(test_swap_math
    screens/test_swap_math.cpp
)
target_include_directories(test_swap_math PRIVATE ../src)
target_link_libraries(test_swap_math PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_swap_math PROPERTIES AUTOMOC ON)
add_test(NAME swap_math COMMAND test_swap_math)

# ── Options strategy max profit / max loss ───────────────────────────────────
# These were read off the sampled payoff curve, whose default window is spot
# +/-30% — so a short put's "max loss" was its loss at the edge of the chart,
# not at S -> 0. A short 24000 PE at 200 reported 350k against a true 1.19m,
# and that figure is what OrderConfirmDialog shows before the order is sent.
add_executable(test_strategy_max_pnl
    services/test_strategy_max_pnl.cpp
    ../src/services/options/StrategyAnalytics.cpp
    ../src/services/options/OptionPricing.cpp
)
target_include_directories(test_strategy_max_pnl PRIVATE ../src)
target_link_libraries(test_strategy_max_pnl PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_strategy_max_pnl PROPERTIES AUTOMOC ON)
add_test(NAME strategy_max_pnl COMMAND test_strategy_max_pnl)

# ── Bar vs event timestamp decoders ──────────────────────────────────────────
# Two kinds of epoch second, both qint64, both called `timestamp`, decoded
# differently — and fromSecsSinceEpoch() looks correct for both. Wrong in five
# places across four files, twice in one file. Fixing instances did not stop it;
# a new one now fails the build.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME bar_timestamp_decoders
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_bar_timestamp_decoders.py
    )
    set_tests_properties(bar_timestamp_decoders PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# ── The `info` payload's contracts ───────────────────────────────────────────
# Two ways a number from yfinance's `info` dict lied. (1) It is routinely
# PARTIAL — beta absent in 148 of 231 cached payloads, the margins in 72 — and
# every absent field arrived as JSON null, which toDouble() reads as 0.0, so a
# third of the time the panels reported a real 0.00% margin. (2) Yahoo reports
# dividendYield as a PERCENTAGE while every consumer multiplies by 100, so MSFT
# showed a 73.00% yield and the portfolio's weighted yield was 100x. Guards
# that absent stays absent, and that the /100 stays on that ONE field — every
# other percentage-like field is already a fraction.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME info_contract
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_info_contract.py
    )
    set_tests_properties(info_contract PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# ── Price-less bars ──────────────────────────────────────────────────────────
# yfinance emits an all-NaN OHLC row for a session it has no prices for yet.
# _sanitize_for_json must turn each NaN into JSON null (bare NaN is invalid
# JSON and Qt rejects the whole document), and QJsonValue::toDouble() then
# reads that null back as 0.0 — so "no price" becomes "priced at zero", one
# zero low drags a chart's y-range to 0, and the log axis flattens every real
# candle into the top 1% of the plot. Guards both halves: that the guard is
# still applied at every builder, and that it stays a finiteness test rather
# than a positivity test (^IRX closed at 0.000; CL=F settled at -37.63).
if(Python3_Interpreter_FOUND)
    add_test(
        NAME priceless_bars
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_priceless_bars.py
    )
    set_tests_properties(priceless_bars PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# ── Unapplied stock splits ───────────────────────────────────────────────────
# On the day a split takes effect Yahoo flips the quote to the new basis before
# it rewrites the historical bars, so history() reports the split in .splits
# while every prior bar is still on the old one. The series then carries a
# one-bar cliff of exactly the split ratio, and the chart, the 52-week band and
# the Technicals rating all read it as a crash — APH came back SELL at -0.399
# the day after its 2-for-1 while TradingView read the same tape at +0.467.
# Pins that detection keys on the ANNOUNCED RATIO, never on the size of the
# gap: real 30%+ earnings moves (ORCL, DELL, MRVL, IONQ all have one) must
# survive untouched, and a split yfinance already applied must not be adjusted
# twice.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME split_repair
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_split_repair.py
    )
    set_tests_properties(split_repair PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# ── AI Forecast reconciliation ───────────────────────────────────────────────
# A model asked for a direction, a target price and a percentage returns three
# figures that need not agree, and the tab stored all three — so the table, the
# chart and the accuracy metric could each quote a different one as "the"
# forecast. Pins that one claim comes out, and that a prediction and an outcome
# are bucketed by the same dead band.
add_executable(test_ai_forecast_math
    screens/test_ai_forecast_math.cpp
)
target_include_directories(test_ai_forecast_math PRIVATE ../src)
target_link_libraries(test_ai_forecast_math PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_ai_forecast_math PROPERTIES AUTOMOC ON)
add_test(NAME ai_forecast_math COMMAND test_ai_forecast_math)

# ── IPO Watch pure parse/format ──────────────────────────────────────────────
# Both helpers have shipped a wrong-but-plausible number: a half-parsed price
# range reported as a confident low side, and a share count run through the
# currency formatter.
add_executable(test_ipo_format
    screens/test_ipo_format.cpp
)
target_include_directories(test_ipo_format PRIVATE ../src)
target_link_libraries(test_ipo_format PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_ipo_format PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME ipo_format COMMAND test_ipo_format)

# ── Model-collapse detection (news briefs) ───────────────────────────────────
# looks_degenerate() is the only thing between a collapsed local model and the
# reading pane, and it shipped blind to the collapse mode that has fresh
# vocabulary but no punctuation — an observed 225-word noun-phrase chain scored
# 0.89 on variety and rendered in full. Both modes are pinned here, along with
# the false-positive cases, which cost more: rejecting a good brief discards it
# silently and shows "AI brief unavailable" instead.
add_executable(test_degeneracy
    ai_chat/test_degeneracy.cpp
)
target_include_directories(test_degeneracy PRIVATE ../src)
target_link_libraries(test_degeneracy PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_degeneracy PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME degeneracy COMMAND test_degeneracy)

# ── News brief formatting (TL;DR + DIGEST panes) ─────────────────────────────
# Everything this covers failed silently: a '•'-bulleted brief lost its whole
# BY CATEGORY pane because the fallback compared against a truncated character
# literal, and a "### DEFENSE, CRYPTO" heading cost the breakdown a category
# outright. Nothing logged; the pane simply held less than the model wrote.
add_executable(test_news_brief_format
    screens/test_news_brief_format.cpp
)
target_include_directories(test_news_brief_format PRIVATE ../src)
target_link_libraries(test_news_brief_format PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_news_brief_format PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME news_brief_format COMMAND test_news_brief_format)

# ── News brief story selection ───────────────────────────────────────────────
# What the brief is written FROM, which is where its two shipped failures
# lived: six outlets carrying one story ate six of the sample's slots, and a
# high-volume category filled every slot so the breakdown could not name the
# others. Both produced a well-formed brief that covered half the day, and
# neither logged anything. Also pins the ordering as a total order — the brief
# is content-addressed, so a selection that reshuffles on equal scores makes
# the cache miss and the brief reword itself for no visible reason.
add_executable(test_news_brief_selection
    services/test_news_brief_selection.cpp
    ../src/services/news/NewsClusterService.cpp
)
target_include_directories(test_news_brief_selection PRIVATE ../src)
target_link_libraries(test_news_brief_selection PRIVATE Qt6::Core Qt6::Network Qt6::Test)
set_target_properties(test_news_brief_selection PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME news_brief_selection COMMAND test_news_brief_selection)

# ── External-link scheme guard ───────────────────────────────────────────────
# open_external_link() sits in front of QDesktopServices::openUrl, which hands
# a URL to the desktop's scheme handler rather than to a browser. Dashboard
# headlines are now clickable and their URLs come from third-party news feeds,
# so the http/https allow-list is a security boundary and is pinned in both
# directions. The accept cases assert on a mirrored predicate rather than
# calling through — a suite that opens ten browser tabs is one nobody runs.
add_executable(test_external_link
    ui/test_external_link.cpp
)
target_include_directories(test_external_link PRIVATE ../src)
target_link_libraries(test_external_link PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(test_external_link PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME external_link COMMAND test_external_link)

# ── OWNERSHIP read-throughs ──────────────────────────────────────────────────
# derive_reads() turns a holder table into sentences, and a sentence is much
# easier to believe than a number. The negative cases carry most of the weight:
# a read that fires when its input is missing is a fabricated claim about a
# real company. Each test starts from a blank snapshot and adds only the field
# it exercises, so any read that appears is provably caused by that field.
add_executable(test_ownership_signals
    screens/test_ownership_signals.cpp
)
target_include_directories(test_ownership_signals PRIVATE ../src)
target_link_libraries(test_ownership_signals PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test_ownership_signals PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME ownership_signals COMMAND test_ownership_signals)

# ── Palette contrast ─────────────────────────────────────────────────────────
# text_dim shipped at 1.91:1 and text_tertiary at 2.53:1 against the ground —
# below WCAG's 3:1 floor for ANY text — and both were used for the lines that
# explain where a number came from. The palette is data, so this is assertable
# without standing up a widget.
add_executable(test_palette_contrast
    ui/test_palette_contrast.cpp
    ../src/ui/theme/Theme.cpp
    ../src/ui/theme/ThemeManager.cpp
    ../src/core/logging/Logger.cpp
)
target_include_directories(test_palette_contrast PRIVATE ../src)
target_link_libraries(test_palette_contrast PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
set_target_properties(test_palette_contrast PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
add_test(NAME palette_contrast COMMAND test_palette_contrast)

# ── Form 4 parsing + insider classification (OWNERSHIP) ──────────────────────
# The screen's whole claim is that its numbers come off the filing. Three of
# these pin bugs found by running against live EDGAR rather than by reading the
# code: the two spellings of SCHEDULE 13D in one index, the filing cap
# colliding with the multi-year classification, and the XSL-wrapped document
# path. The rest pin the rule that a field absent from the filing stays absent
# from the output instead of defaulting to a confident zero.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME ownership_parse
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_ownership_parse.py
    )
endif()

# ── 13F information-table parsing ────────────────────────────────────────────
# Every case here pins something that produced a wrong number when written
# against real filings rather than against the schema: the default XML
# namespace that silently returns an empty book, the same CUSIP filed on
# several rows (Berkshire reports Ally three times) which understates every
# weight, the 2023 move from thousands to whole dollars that is a silent 1000x
# error, and share classes sharing an issuer name.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME thirteen_f_parse
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_13f_parse.py
    )
endif()

# ── 13F bulk ingest ──────────────────────────────────────────────────────────
# Runs the shipped ingest against a synthetic data set shaped exactly like
# SEC's. Every case is a bug that actually shipped: duplicate CUSIP rows within
# one filing (8,404 phantom holders of AAPL), a 13F-HR and its /A both counted,
# puts folded into the share count, the 2023 thousands-to-dollars switch, and an
# absent CIK zero-padded to "0000000000" so unrelated filers merged and one
# vanished from the index.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME thirteen_f_ingest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_13f_ingest.py
    )
endif()

# ── S-1 section slicing ──────────────────────────────────────────────────────
# The slicer anchored on the FIRST heading occurrence, which in a real S-1 is
# the table of contents — so "verbatim excerpts" could be a page-number list.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME s1_sections
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_s1_sections.py
    )
endif()

# ── IPO pipeline: withdrawal detection + readiness clock ─────────────────────
if(Python3_Interpreter_FOUND)
    add_test(
        NAME s1_pipeline
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_s1_pipeline.py
    )
endif()

# ── Credential catalogue sync ────────────────────────────────────────────────
# The catalogue is generated from the scripts that read the keys, and is also
# the allow-list deciding which credentials survive into a child process. A
# key missing from it is DELETED from the subprocess environment, so the data
# source reports "not configured" for a key the user set correctly. This
# fails the build rather than letting that reach a user.

if(Python3_Interpreter_FOUND)
    add_test(
        NAME credential_catalogue_in_sync
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen_credential_catalogue.py --check
    )
endif()
