// src/ai_chat/ChatPersonas.h
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace fincept::ai_chat {

/// A chat "persona": a focused system prompt + a curated tool allow-list (glob
/// patterns over the wire name `<server>__<tool>`, e.g. `int__get_portfolio*`).
///
/// Scoping tools per persona is what makes the assistant usable: handing a 14B
/// model all ~300 registered tools at once wrecks tool selection, so it rarely
/// pulls the right one. A Portfolio persona that sees ~a dozen portfolio tools
/// picks correctly. Empty `tool_globs` = all tools (the General persona, i.e.
/// the legacy behaviour).
struct ChatPersona {
    QString id;
    QString label;
    QString system_prompt;
    QStringList tool_globs;
};

/// Built-in personas. Index 0 (General) preserves the all-tools default.
inline const std::vector<ChatPersona>& builtin_personas() {
    static const std::vector<ChatPersona> kPersonas = {
        {"general", "General",
         "You are finterm AI, a general financial assistant embedded in the terminal. "
         "Use any available tool to help the user.",
         {}},
        {"portfolio", "Portfolio Advisor",
         "You are a portfolio advisor inside finterm. The user's real holdings are reachable through "
         "the portfolio tools — ALWAYS read the actual portfolio (get_portfolio / get_holdings / "
         "get_portfolio_assets / get_transactions) before answering anything about positions, "
         "allocation, risk, or P&L. Cite real numbers and never invent positions.",
         {"int__get_portfolio*", "int__get_holdings", "int__list_portfolios", "int__add_holding",
          "int__update_holding", "int__remove_holding", "int__get_transactions", "int__add_transaction",
          "int__get_quote", "int__run_portfolio_analysis_agent", "int__run_portfolio_rebalancing_agent",
          "int__run_risk_assessment_agent"}},
        {"research", "Research Analyst",
         "You are an equity research analyst inside finterm. Ground every analysis in primary sources: "
         "pull SEC filings with the edgar_* tools and fundamentals / peers / sentiment / news with the "
         "equity tools before drawing conclusions. Be specific and cite the filings you used.",
         {"int__edgar_*", "int__get_equity_*", "int__get_quote", "int__get_candles", "int__get_news",
          "int__search_news", "int__fd_*", "int__search_equity_symbols", "int__load_equity_symbol"}},
        {"markets", "Markets",
         "You are a markets assistant inside finterm. Help with quotes, news, watchlists, and general "
         "market questions using the market-data and news tools; navigate the terminal when asked.",
         {"int__get_quote", "int__get_candles", "int__get_news", "int__search_news", "int__get_top_news",
          "int__get_news_summary", "int__summarize_news_headlines", "int__get_watchlists",
          "int__add_to_watchlist", "int__remove_from_watchlist", "int__navigate_to_tab",
          "int__load_equity_symbol"}},
    };
    return kPersonas;
}

inline const ChatPersona* find_persona(const QString& id) {
    for (const auto& p : builtin_personas())
        if (p.id == id)
            return &p;
    return nullptr;
}

} // namespace fincept::ai_chat
