"""Headlines about a coin, and a count of the words in them.

The same headlines go into the models' prompts and onto the SENTIMENT tab. The
tone is a count of words, said as such: nothing here has read the articles.
"""

from __future__ import annotations

import json
import re

NAMES = {
    "BTC": "Bitcoin", "ETH": "Ethereum", "SOL": "Solana", "XRP": "XRP Ripple", "DOGE": "Dogecoin", "ADA": "Cardano",
    "AVAX": "Avalanche AVAX", "DOT": "Polkadot DOT", "LINK": "Chainlink", "POL": "Polygon POL", "ATOM": "Cosmos ATOM",
    "UNI": "Uniswap", "LTC": "Litecoin", "BCH": "Bitcoin Cash", "APT": "Aptos", "ARB": "Arbitrum",
    "OP": "Optimism OP", "FIL": "Filecoin", "NEAR": "NEAR Protocol", "INJ": "Injective", "SUI": "Sui crypto",
    "SEI": "Sei crypto", "TIA": "Celestia TIA", "RENDER": "Render RNDR", "FET": "Fetch.ai FET", "AAVE": "Aave",
    "SKY": "Sky MakerDAO", "CRV": "Curve CRV", "PEPE": "Pepe PEPE", "WIF": "dogwifhat",
}
_UP = {"surge", "soar", "rally", "gain", "rise", "jump", "climb", "rebound", "recover", "bullish", "beat", "upgrade",
       "outperform", "boom", "breakout", "inflow", "optimism", "adopt", "approve", "record"}
_DOWN = {"plunge", "crash", "fall", "drop", "loss", "lose", "decline", "slump", "slide", "tumble", "bearish", "weak",
         "downgrade", "fear", "outflow", "selloff", "liquidation", "hack", "exploit", "lawsuit", "fraud", "sink"}
_WORD = re.compile(r"[a-z]+")


def _stems(word: str) -> set[str]:
    """A word and what it may be a form of: rallies, rallied -> rally; falling -> fall."""
    out = {word}
    for end, put in (("ies", "y"), ("ied", "y"), ("es", ""), ("es", "e"), ("s", ""), ("ed", ""), ("ed", "e"),
                     ("ing", ""), ("ing", "e"), ("ping", ""), ("ped", "")):
        if word.endswith(end) and len(word) - len(end) >= 2:
            out.add(word[: -len(end)] + put)
    return out | ({"fall"} if word in ("fell", "fallen") else {"rise"} if word in ("rose", "risen") else
                  {"lose"} if word == "lost" else {"sink"} if word in ("sank", "sunk") else set())


def tone(text: str) -> str:
    up = down = 0
    for word in _WORD.findall(text.lower().replace("sell-off", "selloff")):
        stems = _stems(word)
        up += bool(stems & _UP)
        down += bool(stems & _DOWN)
    return "positive" if up > down else "negative" if down > up else "neutral"


def headlines(symbol: str, limit: int = 12) -> list[dict]:
    """[{title, publisher, published, url, tone}], the last two days. Raises on failure."""
    from fetch_company_news import fetch_company_news

    coin = symbol.split("/")[0]
    answer = json.loads(fetch_company_news(NAMES.get(coin, f"{coin} crypto"), limit, "2d"))
    if not answer.get("success"):
        raise RuntimeError(str(answer.get("error") or "no news"))
    out = []
    for a in answer.get("data") or []:
        title = str(a.get("title") or "").encode("utf-8", "replace").decode("utf-8").strip()
        publisher = str(a.get("publisher") or "").strip()
        if publisher and title.endswith(" - " + publisher):  # as Google News writes it
            title = title[: -len(publisher) - 3]
        if title:
            out.append({"title": title, "publisher": publisher, "published": str(a.get("published_date") or ""),
                        "url": str(a.get("url") or ""), "tone": tone(title)})
    return out
