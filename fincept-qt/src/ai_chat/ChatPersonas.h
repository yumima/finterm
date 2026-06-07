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
/// model all ~474 registered tools at once (~107k tokens) overruns the request
/// and wrecks tool selection, so it rarely pulls the right one. A Portfolio
/// persona that sees ~a dozen portfolio tools picks correctly. Empty
/// `tool_globs` still means "all tools", but the count is hard-capped in
/// McpService::format_tools_for_openai so it can never blow up a request.
struct ChatPersona {
    QString id;
    QString label;
    QString system_prompt;
    QStringList tool_globs;
};

/// Built-in personas. Index 0 (General) is a broad-but-curated default — it
/// carries the most-used market / news / portfolio tools rather than all ~474,
/// which used to hang the local model on every message.
inline const std::vector<ChatPersona>& builtin_personas() {
    static const std::vector<ChatPersona> kPersonas = {
        {"general", "General",
         "You are finterm AI, a general financial assistant embedded in the terminal. You can call "
         "the available market, news, portfolio, and watchlist tools directly — prefer calling a "
         "tool to fetch live data over answering from memory, and never claim you lack access to "
         "data the tools can fetch.",
         {"int__get_quote", "int__get_candles", "int__get_news", "int__search_news",
          "int__get_top_news", "int__get_news_summary", "int__get_portfolio*", "int__get_holdings",
          "int__list_portfolios", "int__get_transactions", "int__get_watchlists",
          "int__add_to_watchlist", "int__remove_from_watchlist", "int__navigate_to_tab",
          "int__load_equity_symbol", "int__search_equity_symbols"}},
        {"portfolio", "Portfolio Advisor",
         "You are a portfolio advisor inside finterm. The user's real holdings live behind the "
         "portfolio tools, which you can call directly. For ANY question about the user's portfolio, "
         "holdings, positions, allocation, risk, or P&L you MUST first call list_portfolios to find "
         "their portfolio(s), then get_holdings / get_portfolio / get_transactions for the details — "
         "do this before you answer. NEVER claim you lack access to the portfolio or ask the user "
         "for a portfolio id or account details: you have direct tool access, so call the tools. If "
         "list_portfolios returns nothing, tell the user they have not set up a portfolio yet. Cite "
         "real numbers and never invent positions.",
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
