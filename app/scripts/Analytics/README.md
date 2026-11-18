# Analytics Modules

> Financial analytics the app runs through `PythonRunner`

Every package here has a C++ caller; the entry point named in each row is the
script the app runs (as `python -m Analytics.<pkg>.<module>` when the path has
a `/`). A module nothing runs or imports is deleted, not kept for reference.

## Packages

| Package | Entry point (run by) | Covers |
|---------|----------------------|--------|
| `alternateInvestment/` | `cli.py` (Alt Investments screen, MCP alt-investment tools) | Real estate, hedge funds, private capital, commodities, digital assets, annuities, structured products, bonds |
| `backtesting/` | `<provider>/<provider>_provider.py` for `backtestingpy`, `bt`, `fasttrade`, `vectorbt`, `zipline` (Backtesting screen) | Backtest, optimize, walk-forward, indicators; shared base in `base/` |
| `corporateFinance/` | one script per model (M&A Analytics screen) | DCF, trading comps, precedent transactions, LBO, merger models, synergies, deal structure, startup valuation, fairness opinion, industry metrics, deal database |
| `economics/` | `trade_geopolitics.py` (Geopolitics) | Trade, currency, growth, policy, capital flows, market cycles |
| `quant/` | imported by `quant_analytics_cli.py` (AI Quant Lab "CFA Quant") | CFA quantitative methods, input validation |
| `statsmodels_wrapper/` | imported by `statsmodels_cli.py` (AI Quant Lab) | Econometrics |
| `talipp_wrapper/` | imported by `../equity_talipp.py` | Streaming technical indicators |
| `gs_quant_wrapper/` | `gs_quant_service.py` (AI Quant Lab) | Risk, portfolio analytics, Greeks, VaR, stress tests |
| `functime_wrapper/` | `functime_service.py` (AI Quant Lab) | Time-series forecasting |
| `fortitudo_tech_wrapper/` | `fortitudo_service.py` (AI Quant Lab) | Risk-aware portfolio optimization |
| `gluonts_wrapper/` | `gluonts_service.py` (AI Quant Lab) | Probabilistic forecasting |

Top-level scripts: `quant_analytics_cli.py`, `statsmodels_cli.py` and
`technical_indicators.py` (AI Quant Lab).

## Technical Standards

- **Language**: Python 3.11+
- **Dependencies**: NumPy, Pandas, SciPy, plus each wrapper's library
- **Output**: JSON on stdout
