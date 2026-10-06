# finterm data-integrity audit — 2026-10-05

Read-only audit (6 parallel auditors + spot verification). Paths relative to `fincept-qt/`.
✔ = re-verified by reading the code after the auditor reported it.

## Tier 1 — fabricated data shown as real

### Fallbacks that invent data when a fetch fails
- ✔ Backtests: every external engine (vectorbt, backtesting.py, bt, zipline, fasttrade) swaps in random GBM prices on fetch failure/empty data; `using_synthetic_data` flag is never read by `BacktestingScreen.cpp` → normal-looking Sharpe/CAGR on fake prices. `scripts/Analytics/backtesting/*/…_data.py`, `*_provider.py`.
- ✔ finterm engine `History()` returns a fake straight line (`p = base_price + i*0.01`) — `scripts/strategies/fincept_engine/algorithm.py:1665,1681`; warm-up/ML training of ~a dozen registry strategies runs on it.
- ✔ Portfolio: failed price fetch → flat $1.00 series (stock and SPY benchmark show +0.00%) — `src/services/portfolio/PortfolioService.cpp:1299-1317`; intraday same at `:1415-1435`.
- Portfolio 1D NAV paints failed symbols flat at cached/avg cost and sums mixed currencies without FX — `PortfolioService.cpp:1503-1569,1633`.
- Portfolio cold start: no quote → `current_price = avg_buy_price` (P&L 0) shown with no estimate flag — `PortfolioService.cpp:709-714`.
- ✔ Crypto paper market order fills at invented $1000 when no cached ticker — `src/screens/crypto_trading/CryptoTradingScreen.cpp:785,795`.
- Equity paper market order with no quote fills at stale typed limit price — `EquityTradingScreen.cpp:918,932`.
- yfinance quote: missing prev close → `prev_close = current` → fake 0.00% day change — `scripts/yfinance_data.py:487,535,1589-1590`; NaN volume → 0 `:1608`. Same pattern `futures_router.py:307-321`.
- Crypto MCP tools: exchange error → success with last/bid/ask = 0 — `src/mcp/tools/CryptoTradingTools.cpp:28-41`, `ExchangeSession.cpp:451-466`.

### Hardcoded / synthetic values presented as live
- ✔ Maritime trade corridors (`$156B`, "critical", vessel counts) — `src/services/maritime/MaritimeTypes.h:86-99`; stat tiles `TRADE VOL $847.3B`, `SATELLITES 13`, `THREAT: LOW` — `MaritimeScreen.cpp:253,315-327`. Vessel API removed, so nothing ever replaces them.
- ✔ Geopolitics Relationship Network dataset counts/severities — `src/screens/geopolitics/RelationshipPanel.cpp:17-35`.
- ✔ Portfolio correlation matrix computed from today's day-change signs — `src/screens/portfolio/views/AnalyticsSectorsView.cpp:752-757` (a real one exists: `PortfolioService::fetch_correlation`).
- ✔ Portfolio NAV fallback: 30 fake days with `sin()` wiggle — `views/PerformanceRiskView.cpp:226-238`; fake "yesterday" point = cost basis — `PortfolioPerfChart.cpp:1325-1333`.
- Portfolio factor-sensitivity table: uncited constant betas, hardcoded sector map — `views/EconomicsView.cpp:103-232,305-371`.
- Dashboard Market Pulse TOP GAINERS/LOSERS from a fixed 12-ticker list — `MarketPulsePanel.cpp:84,915`, `MarketDataService.cpp:1417`; "NYSE/NASDAQ/S&P 500 breadth" from a ~40-stock basket — `MarketPulsePanel.cpp:805-840`.
- Surface Analytics opens on `rand()` demo surfaces (badged "SYNTHETIC DATA", but still violates zero-fake rule) — `SurfaceAnalyticsScreen.cpp:87-99,373-431`, `SurfaceDemoData.cpp`.
- Futures "Init. Margin" = undated late-2025 CME snapshot — `src/screens/futures/FuturesContracts.h:76-117`.
- Report Builder templates ship invented charts under real names (AAPL segments, peer P/Es, etc.) — `src/services/report_builder/ReportBuilderTemplates.cpp:157,262,267,285,326,394,467…`; fed back to LLM via `report_get_state`.
- Alt Investments forms prefilled with stale "market" values for real assets (BTC $45k, AAPL $150, WTI $80…) — `AltInvestmentsScreen.cpp:256-445`.
- ✔ Relationship map peers: substring industry dict, fallback AAPL/MSFT/GOOGL/AMZN/META for anything unmatched — `scripts/relationship_map.py:343-375`; missing values → 0.0 (`P/E 0.0`, `$0.00`) `:16`.
- ✔ IPO Watch: S-1 fee-table `dollarValueOfSharesOffered` shown as **Deal size** for filed rows (violates S-1 rule) — `src/screens/pre_ipo/IpoWatchView.cpp:929-935`, display `:1572,1754,3278`, filter `:1303`; `scripts/nasdaq_data.py:647`.

### Workflow engine (node editor) fakes outcomes
- ✔ Place Order returns `status: submitted` + timestamp ID; nothing reaches any broker — `src/services/workflow/adapters/ServiceBridges.cpp:113-123`.
- ✔ All triggers hardcode `triggered = true` — `TriggerNodes.cpp:35,60,87,116,146`, `UtilityNodes.cpp:704`; "Price Alert Auto-Trade" template buys unconditionally.
- 13 nodes (cancel/modify/get_orders/positions/balance/close_position/bracket…) are pass-through success stubs — `ServiceBridges.cpp:710-720`.
- Safety nodes fail open on missing data (risk_check, correlation_check, max_drawdown…) — `SafetyNodes.cpp`.
- Options Chain / Insider Trades / SEC Filings nodes return yfinance `info` under those labels; Screener ignores criteria; Market Depth fabricates bid/ask — `MarketDataNodes.cpp:128,333,430,467,512`.
- Regime Detection says HMM, is a fixed heuristic; Risk Analysis echoes chosen method but always computes historical VaR — `AnalyticsNodes.cpp:286-366,804-890`.

### Agents (scripts/agents/finagent_core)
- Rebalancing/Risk workflows run with no portfolio data; LLM invents holdings/VaR — `main.py:685,694`; rebalance orders always generated `workflow_module.py:257-271`.
- macro/earnings/sector/options/sentiment scans run tool-less, answers are training recall — `main.py:699-763`.
- Fallback plan analyses AAPL — `execution_planner.py:711`.
- Named agents' system prompts seeded under `system_prompt` but read from `instructions` → run as "You are a helpful AI assistant." — `v028_named_agents.cpp`, `AgentNodes.cpp:93`, `config_loader.py:90`.

## Tier 2 — wrong calculations / wrong units

- ✔ Economics LATEST/CHANGE assume oldest-first; BLS & FiscalData are newest-first → oldest value shown, sign flipped — `src/screens/economics/panels/EconPanelBase.cpp:407`; value-column guess `keys[1]` picks wrong FiscalData field `:383-386`.
- ✔ FRED panel never shows data: script emits floats, panel reads `.toString()` → "" — `FredPanel.cpp:116` vs `scripts/fred_data.py:110`.
- BLS % change stored as fraction under `change_percent_*`; M13 annual rows dated `YYYY-13-01` — `bls_data.py:336-340`.
- ✔ F&O Builder POP always σ=20% (chain IV not passed) — `BuilderSubTab.cpp:248`, `StrategyAnalytics.cpp:351`; rf 6.7% hardcoded, user setting ignored in Builder; Greeks show 0 not "—"; Vega ×100 unlabeled; expiry-day T floored to 1 day; IV percentile mixes expiries.
- Kalshi probability = best yes **bid** — `KalshiRestClient.cpp:79,82`.
- Bond pricing wrong (drops stub coupon; 10y uses 19 periods) — `scripts/derivatives_pricing.py:130-200`; CDS r=5% "for demo" `:283`.
- Databento surfaces: yield `(100 - price/100)*2` (ZN≈110 → ~198%), "Dupire" local vol is `iv*(1+0.1(m-1)^2)`, commodity vol 30d row always 0, crack labelled 3-2-1 is 1:1, USDJPY via 6J inverted, missing → 0.0 — `scripts/databento_provider.py:1776-2237`.
- Financials tab cards: missing → "0"/"0.00%" (bank gross margin, negative-equity ROE); "$" on non-USD statements; silent 21% tax in ROIC — `EquityFinancialsTab.cpp:707-913,787`.
- Alt Investments: price fields ignored (key mismatch), coupon sent unscaled (8.5 → 850%, spreads 84,600 bps) — `AltInvestmentsScreen.cpp:256-286`, `alternateInvestment/cli.py`; all 27 Alt MCP tools broken (command names).
- Portfolio: rf 4% hardcoded fallback & ^TNX labelled "risk-free"; "VOL 30D" over up to 365d; weekend/gap snapshots treated as daily; UTC trade date vs local snapshot date → phantom gain/loss; VaR95 from <20 returns; indexed-vs-benchmark uses raw NAV incl. deposits; "1Y" with 7 months; "TOTAL"/"RETURN" unrealized only; stress-test LOSS always "-" (✔ `RiskManagementView.cpp:337`); risk score `beta.value_or(1.0)`.
- QuantStats drops day-1 return; NaN/inf → 0.0 in quantstats/ffn/monte_carlo scripts; FFN mislabelled ERC fallback. (Note: `PortfolioAnalyticsService` passes script names without `.py` and scripts read stdin while runner passes argv — these views may never run at all; code-traced, not runtime-verified.)
- News sentiment substring matching ("ban"∈bank, "gain"∈again); "85% conf" is a table literal; volume z-score shown as "x" multiplier — `NewsService.cpp:1366-1389,1500-1576`.
- Market Pulse market-hours ignore DST/half-hours — `MarketPulsePanel.cpp:749-777`.
- Treasury MIN/MAX "yield" is coupon rate (bills 0.000%) — `GovDataTreasuryPanel.cpp:179-181`; offering truncated `:454`.
- EDGAR insider counts M as buy, F as sell — `forms_insider.py:134,210-216`.
- IPO "POP %" is IPO-to-today return; labels show literal `%%`.
- gs_quant greeks/VaR/stress always error (arg order) — `gs_quant_service.py:388,406,440`.
- `technical_indicators.py` only has `test` on random data; RSI/ATR/ADX non-Wilder (real path via `ta` library is correct).

## Tier 3 — AI grounding / error-as-success

- ✔ AI chat never receives today's date (no date in any finterm prompt).
- Starter prompts ("today's top movers", "current GDP") have no tool in the General persona allowlist; base prompt says "always use a tool, never decline" → answers from memory — `AiChatScreen.cpp:630-635`, `ChatPersonas.h:31-38`.
- Base prompt claims Report Builder/EDGAR/Python tools no persona exposes; contains example figures small models copy — `LlmService.cpp:242-277`.
- AppContextService "current view" never cleared/timestamped.
- ✔ EDGAR errors returned as success + cached 30 min — `EdgarTools.cpp:53`; same for GovData (5 min), M&A (`MAAnalyticsTools.cpp:55-58`), QuantLab, portfolio optimization node.
- M&A: ~30 modules can't run (command/argv mismatch) — `MAAnalyticsService.cpp:127-190`.
- News Analyze fills missing sentiment with 0.00; `analyze_news_article` still talks about API credits.
- AI Tutor "example" action asks for "plausible numbers"; "apply" uses a canned 60/30/10 portfolio.
- `ds_list_connections` exposes raw API keys to the LLM.

## Process note
One sub-auditor ran `database_schema.py create` in the app venv against the real M&A sqlite DB; it reports the INSERT failed (`no column named acquirer`), so nothing was written. Not independently confirmed.

## Checked and clean (per auditors)
13F units across the 2023 change; Form 4 parsing; pre-IPO curated valuations (source/as-of/round on every seed entry); ledger avg-cost/realized/splits/dividends; TWR chaining; date-aligned beta; BSM/py_vollib Greeks (theta/day); max pain, PCR, breakevens; Polymarket pricing; WorldBank/EIA/DBnomics pass-through; TopMovers widget (real screener); 101 reachable top-level fetchers (no hardcoded live values); `ta`-based technicals; Monte Carlo views (labelled simulations).

## Not covered
Equity research Overview/Peers/Earnings/Technicals tabs line-by-line; ~25 economics panels individually (likely share the EconPanelBase ordering bug); AlphaArena; AI Quant Lab beyond script mapping; individual broker adapters; portfolio Optimization/Futures/CustomIndex views.
