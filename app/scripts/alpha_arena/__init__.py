"""Alpha Arena: AI models trade the same market with paper money.

main.py is the app's entry point (AlphaArenaService); one process per action.

    market.py    prices: exchanges (crypto), Polymarket
    features.py  indicators from hourly closes
    news.py      headlines about an asset
    book.py      a trader's portfolio, worked out from its fills
    llm.py       asking a model
    traders.py   the prompts, and a decision out of an answer; the grid bot
    arena.py     a cycle: prices, decisions, fills, standings
    store.py     the database
"""
