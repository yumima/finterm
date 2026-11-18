# Python Scripts Library

> Comprehensive collection of data sources, analytics, and AI agents for finterm

## Overview

This directory contains Python scripts powering the backend analytics, data integrations, and AI capabilities of finterm. All scripts are executed by the C++ application via the Python bridge (python_runner.cpp).

## Directory Structure

```
scripts/
├── agents/              # AI agents for trading and geopolitical analysis
├── agno_trading/        # Technical-indicator tools Alpha Arena agents use
├── ai_quant_lab/        # Quantitative research lab (Qlib, RDAgent)
├── Analytics/           # Financial analytics modules
├── *.py                 # Data source integrations
└── README.md           # This file
```

## Quick Links

### Module Documentation

| Category | Description | Link |
|----------|-------------|------|
| 📊 **Analytics** | Corporate finance, alternatives, backtesting, quant wrappers | [Analytics/README.md](./Analytics/README.md) |
| 🤖 **AI Agents** | 30+ agents - hedge funds, investors, geopolitics | [agents/README.md](./agents/README.md) |
| 🔬 **AI Quant Lab** | Qlib + RDAgent - automated strategy research | [ai_quant_lab/README.md](./ai_quant_lab/README.md) |
| 🚀 **Agno Trading** | Technical-indicator tools (used by Alpha Arena) | `agno_trading/` |

## Key Features

### Data Integration
- **Market Data**: Yahoo Finance, Databento
- **Economic Data**: FRED, World Bank, IMF, OECD, ECB, BEA, BLS
- **Crypto**: CoinGecko, Kraken, Binance
- **Government**: SEC Edgar, Congress.gov, Federal Reserve
- **International**: AkShare (China), Eurostat (EU), data.gov variants

### Analytics Modules
- **Corporate Finance**: DCF, comps, precedent transactions, LBO, merger models
- **Economics**: Trade & geopolitics
- **Alternative Investments**: Real estate, hedge funds, private capital, crypto
- **Quantitative**: CFA quant models, statsmodels, gs-quant, forecasting wrappers

### AI & Machine Learning
- **Geopolitical Agents**: Grand Chessboard, Prisoners of Geography frameworks
- **Investor Personas**: Warren Buffett, Benjamin Graham strategies
- **Hedge Fund Agents**: Bridgewater, Citadel, Renaissance, Two Sigma
- **Quant Lab**: Qlib integration, RDAgent for hypothesis generation

### Backtesting Frameworks
- **Backtesting.py**: Flexible Python backtesting
- **VectorBT**: High-performance vectorized backtesting
- **FastTrade**: Lightweight backtesting library
- **bt**, **Zipline**

## Usage Pattern

Scripts are invoked from the Qt/C++ application via `PythonRunner`:

```cpp
// Scripts are called via src/python/PythonRunner.cpp

// Example: a script file under scripts/, then its CLI arguments
finterm::python::PythonRunner::instance().run(
    "fred_data.py",
    {"series", "GDP"},
    [](const finterm::python::PythonResult& result) {
        // handle result
    }
);
```

## Development Guidelines

### Adding New Data Sources
1. Create `{source}_data.py` in scripts root
2. Implement standardized response format
3. Wire the script into the relevant Qt service (`src/services/`) or screen — a script nothing runs is deleted

### Adding Analytics Modules
1. Place in appropriate `Analytics/` subdirectory
2. Follow CFA curriculum structure
3. Include docstrings and type hints

### Adding AI Agents
1. Add to `agents/` with appropriate subdirectory
2. Use FinAgent core framework
3. Define persona and strategy

## Technical Requirements

- **Python Version**: 3.11+
- **Execution**: Embedded Python runtime bundled with app
- **IPC**: Qt/C++ ↔ Python via `PythonRunner` (QProcess-based)
- **Output Format**: JSON responses
- **Error Handling**: Structured error objects

## Project Context

Part of **finterm** - a financial intelligence platform built with:
- **UI**: C++20 + Qt6 Widgets
- **Core**: C++20
- **Analytics**: Python (embedded runtime)
- **AI**: Ollama (local LLM), Langchain, multi-provider LLM

## Documentation

- **Root CLAUDE.md**: `../../CLAUDE.md`
- **App CLAUDE.md**: `../../../CLAUDE.md`
- **Architecture**: `../../../../docs/ARCHITECTURE.md`

## Performance Notes

- Scripts execute via Qt `QProcess` through `PythonRunner` (max 3 concurrent)
- Large datasets should stream or paginate results
- Cache frequently accessed data when possible
- Use async/await patterns in frontend for better UX

## License

MIT License - Part of finterm

---

**Last Updated**: 2026-11-17
