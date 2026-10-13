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
         "the available market, movers, news, macro (DBnomics), SEC-filing valuation, portfolio, and "
         "watchlist tools directly — call a tool to fetch live data instead of answering from memory. "
         "If none of your tools covers what was asked, say you don't have that data here.",
         // No navigation tools (navigate_to_tab / load_equity_symbol): General
         // is the persona of the floating bubble and of persona-less callers
         // (workflow nodes, MCP sampling) — surfaces that float OVER whatever
         // the user is doing. A model yanking the terminal to another screen
         // mid-question is a hijack, not help, and navigation is not
         // destructive-flagged so no confirmation gate would catch it.
         {"int__get_quote", "int__get_equity_historical", "int__get_candles", "int__get_top_movers",
          "int__get_news", "int__search_news", "int__get_top_news", "int__get_news_summary", "int__search_dbnomics", "int__list_dbnomics_series",
          "int__get_dbnomics_observations", "int__edgar_calc_multiples", "int__edgar_get_financial_metrics",
          "int__get_portfolio*", "int__get_holdings", "int__list_portfolios", "int__get_transactions",
          "int__get_watchlists", "int__add_to_watchlist", "int__remove_from_watchlist",
          "int__search_equity_symbols"}},
        {"portfolio", "Portfolio Advisor",
         "You are a portfolio advisor inside finterm. The user's real holdings live behind the "
         "portfolio tools, which you can call directly. For ANY question about the user's portfolio, "
         "holdings, positions, allocation, risk, or P&L you MUST first call list_portfolios to find "
         "their portfolio(s), then get_holdings / get_portfolio / get_transactions for the details — "
         "do this before you answer. NEVER claim you lack access to the portfolio or ask the user "
         "for a portfolio id or account details: you have direct tool access, so call the tools. If "
         "list_portfolios returns nothing, tell the user they have not set up a portfolio yet. Cite "
         "real numbers from the tool results and never invent positions or figures.",
         {"int__get_portfolio*", "int__get_holdings", "int__list_portfolios", "int__add_holding",
          "int__update_holding", "int__remove_holding", "int__get_transactions", "int__add_transaction",
          "int__get_quote", "int__run_portfolio_analysis_agent", "int__run_portfolio_rebalancing_agent",
          "int__run_risk_assessment_agent"}},
        {"equity", "Stock Analyst",
         "You are a sharp equity analyst answering questions about ONE specific stock for a retail "
         "investor. A DATA block with today's real figures, recent headlines and the terminal's own "
         "AI analyses for the ticker leads the conversation; the DATA block's ticker is the subject "
         "even when the user types it loosely or misspells it. Work in this order: (1) answer from "
         "the DATA block when it carries the answer; (2) otherwise CALL YOUR TOOLS FIRST and answer "
         "from their results — get_news / search_news for why the stock moved or anything "
         "event-driven, edgar_* for filings and fundamentals detail, get_quote / get_candles for "
         "prices, get_top_movers / get_top_news for market-wide context (sector selloff vs "
         "company-specific). Never ask permission to use a tool and never end with an offer like "
         "\"Would you like me to pull those?\" — pull them, then answer. Explaining a daily move "
         "without having read that day's headlines is a guess; do not present a guess as analysis. "
         "If a figure or fact is in neither the DATA block nor a tool result, say plainly that you "
         "don't have it — never guess or invent numbers or explanations. (A private / pre-IPO "
         "company often has no public revenue or fundamentals — say so.) Stay on this stock: the "
         "market-wide tools exist to explain ITS moves, not for general market commentary. Be "
         "concrete and balanced, quantify when you can, and flag the key risk. Informational, not "
         "financial advice.",
         {"int__get_quote", "int__get_candles", "int__get_equity_*", "int__get_news", "int__search_news",
          "int__get_top_news", "int__get_news_summary", "int__get_top_movers", "int__edgar_*",
          "int__search_equity_symbols"}},
        // "research", "markets" and "fingpt" were retired with the persona
        // picker: nothing can select them since the chat screen was removed,
        // and the FinGPT engine now lives in Equity Research's AI tab with its
        // real prompts (FinGptPrompts.h) rather than a persona imitation. A
        // persona here must have a surface that mounts it, or it is prompt
        // text that silently drifts from the live code it describes.
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
