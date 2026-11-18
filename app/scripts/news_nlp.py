"""
News NLP Pipeline — Entity extraction, semantic clustering.
Called by NewsNlpService via PythonRunner.

Commands:
  extract_entities <json_headlines>
  cluster_semantic <json_headlines>
"""
import sys
import json
import re
import math
from collections import Counter

# ── Country/Region database ──────────────────────────────────────────────────

COUNTRIES = {
    "united states": "US", "usa": "US", "u.s.": "US", "america": "US",
    "china": "CN", "chinese": "CN", "beijing": "CN", "shanghai": "CN",
    "russia": "RU", "russian": "RU", "moscow": "RU", "kremlin": "RU",
    "ukraine": "UA", "ukrainian": "UA", "kyiv": "UA", "kiev": "UA",
    "united kingdom": "GB", "britain": "GB", "british": "GB", "london": "GB", "uk": "GB",
    "germany": "DE", "german": "DE", "berlin": "DE",
    "france": "FR", "french": "FR", "paris": "FR",
    "japan": "JP", "japanese": "JP", "tokyo": "JP",
    "india": "IN", "indian": "IN", "mumbai": "IN", "delhi": "IN",
    "brazil": "BR", "brazilian": "BR",
    "canada": "CA", "canadian": "CA",
    "australia": "AU", "australian": "AU",
    "south korea": "KR", "korean": "KR", "seoul": "KR",
    "north korea": "KP", "pyongyang": "KP",
    "iran": "IR", "iranian": "IR", "tehran": "IR",
    "israel": "IL", "israeli": "IL", "tel aviv": "IL",
    "palestine": "PS", "palestinian": "PS", "gaza": "PS", "west bank": "PS",
    "saudi arabia": "SA", "saudi": "SA", "riyadh": "SA",
    "turkey": "TR", "turkish": "TR", "ankara": "TR", "istanbul": "TR",
    "egypt": "EG", "egyptian": "EG", "cairo": "EG",
    "italy": "IT", "italian": "IT", "rome": "IT",
    "spain": "ES", "spanish": "ES", "madrid": "ES",
    "mexico": "MX", "mexican": "MX",
    "indonesia": "ID", "indonesian": "ID", "jakarta": "ID",
    "netherlands": "NL", "dutch": "NL",
    "switzerland": "CH", "swiss": "CH",
    "sweden": "SE", "swedish": "SE",
    "norway": "NO", "norwegian": "NO",
    "poland": "PL", "polish": "PL", "warsaw": "PL",
    "taiwan": "TW", "taiwanese": "TW", "taipei": "TW",
    "singapore": "SG",
    "hong kong": "HK",
    "south africa": "ZA",
    "nigeria": "NG", "nigerian": "NG", "lagos": "NG",
    "argentina": "AR",
    "colombia": "CO",
    "pakistan": "PK", "pakistani": "PK",
    "thailand": "TH", "thai": "TH", "bangkok": "TH",
    "vietnam": "VN", "vietnamese": "VN",
    "philippines": "PH", "filipino": "PH",
    "malaysia": "MY", "malaysian": "MY",
    "iraq": "IQ", "iraqi": "IQ", "baghdad": "IQ",
    "syria": "SY", "syrian": "SY", "damascus": "SY",
    "yemen": "YE", "yemeni": "YE",
    "libya": "LY", "libyan": "LY",
    "lebanon": "LB", "lebanese": "LB", "beirut": "LB",
    "afghanistan": "AF", "afghan": "AF", "kabul": "AF",
    "myanmar": "MM", "burmese": "MM",
    "ethiopia": "ET", "ethiopian": "ET",
    "sudan": "SD", "sudanese": "SD", "khartoum": "SD",
    "somalia": "SO", "somali": "SO",
    "venezuela": "VE", "venezuelan": "VE",
    "cuba": "CU", "cuban": "CU", "havana": "CU",
    "european union": "EU", "eu": "EU", "brussels": "EU",
}

# ── Organization patterns ────────────────────────────────────────────────────

ORGANIZATIONS = {
    "nato": "NATO", "united nations": "UN", "un": "UN",
    "imf": "IMF", "world bank": "World Bank",
    "federal reserve": "Federal Reserve", "the fed": "Federal Reserve",
    "ecb": "ECB", "european central bank": "ECB",
    "bank of england": "Bank of England", "boe": "Bank of England",
    "bank of japan": "Bank of Japan", "boj": "Bank of Japan",
    "peoples bank of china": "PBOC", "pboc": "PBOC",
    "sec": "SEC", "securities and exchange": "SEC",
    "opec": "OPEC", "opec+": "OPEC+",
    "world health organization": "WHO", "who": "WHO",
    "world trade organization": "WTO", "wto": "WTO",
    "pentagon": "Pentagon", "white house": "White House",
    "congress": "US Congress", "senate": "US Senate",
    "european commission": "European Commission",
    "iaea": "IAEA", "g7": "G7", "g20": "G20", "brics": "BRICS",
    "reuters": "Reuters", "bloomberg": "Bloomberg",
    "apple": "Apple", "google": "Google", "alphabet": "Alphabet",
    "microsoft": "Microsoft", "amazon": "Amazon", "meta": "Meta",
    "nvidia": "NVIDIA", "tesla": "Tesla", "openai": "OpenAI",
    "goldman sachs": "Goldman Sachs", "jpmorgan": "JPMorgan",
    "blackrock": "BlackRock", "berkshire hathaway": "Berkshire Hathaway",
    "hsbc": "HSBC", "ubs": "UBS", "credit suisse": "Credit Suisse",
    "deutsche bank": "Deutsche Bank", "barclays": "Barclays",
    "citigroup": "Citigroup", "morgan stanley": "Morgan Stanley",
}

# ── Person title patterns ────────────────────────────────────────────────────

PERSON_TITLES = [
    r"\b(president|prime minister|chancellor|king|queen|prince|princess)\s+([A-Z][a-z]+(?:\s+[A-Z][a-z]+)*)",
    r"\b(ceo|cfo|chairman|director|minister|secretary|governor|general)\s+([A-Z][a-z]+(?:\s+[A-Z][a-z]+)*)",
    r"\b(mr|mrs|ms|dr|prof)\.\s+([A-Z][a-z]+(?:\s+[A-Z][a-z]+)*)",
]

# Known leaders
KNOWN_PERSONS = {
    "biden": "Joe Biden", "trump": "Donald Trump", "obama": "Barack Obama",
    "putin": "Vladimir Putin", "xi jinping": "Xi Jinping", "xi": "Xi Jinping",
    "modi": "Narendra Modi", "macron": "Emmanuel Macron",
    "scholz": "Olaf Scholz", "sunak": "Rishi Sunak", "starmer": "Keir Starmer",
    "kishida": "Fumio Kishida", "trudeau": "Justin Trudeau",
    "zelensky": "Volodymyr Zelensky", "zelenskyy": "Volodymyr Zelensky",
    "netanyahu": "Benjamin Netanyahu",
    "erdogan": "Recep Tayyip Erdogan",
    "musk": "Elon Musk", "bezos": "Jeff Bezos", "zuckerberg": "Mark Zuckerberg",
    "cook": "Tim Cook", "nadella": "Satya Nadella", "altman": "Sam Altman",
    "dimon": "Jamie Dimon", "powell": "Jerome Powell", "lagarde": "Christine Lagarde",
    "yellen": "Janet Yellen",
}


def extract_entities(headlines_json):
    """Extract countries, organizations, people, and tickers from headlines."""
    try:
        articles = json.loads(headlines_json) if isinstance(headlines_json, str) else headlines_json
    except json.JSONDecodeError:
        return {"success": False, "error": "Invalid JSON"}

    all_countries = Counter()
    all_orgs = Counter()
    all_people = Counter()
    all_tickers = Counter()
    per_article = []

    for article in articles:
        headline = article.get("headline", "")
        summary = article.get("summary", "")
        text = (headline + " " + summary).lower()
        text_orig = headline + " " + summary

        countries = []
        orgs = []
        people = []
        tickers = []

        # Country extraction — use word-boundary matching to avoid substring hits
        # (e.g. "rome" inside "jerome", "uk" inside "truck", "thai" inside "thailand")
        for pattern, code in COUNTRIES.items():
            if len(pattern) <= 4 or " " not in pattern:
                # Short or single-word patterns: require word boundaries
                if re.search(r'\b' + re.escape(pattern) + r'\b', text):
                    countries.append({"name": pattern.title(), "code": code})
                    all_countries[code] += 1
            else:
                # Multi-word patterns: simple substring is fine
                if pattern in text:
                    countries.append({"name": pattern.title(), "code": code})
                    all_countries[code] += 1

        # Organization extraction
        for pattern, name in ORGANIZATIONS.items():
            if pattern in text:
                orgs.append(name)
                all_orgs[name] += 1

        # Person extraction — known persons
        for pattern, name in KNOWN_PERSONS.items():
            if pattern in text:
                people.append(name)
                all_people[name] += 1

        # Person extraction — title patterns
        for pat in PERSON_TITLES:
            for match in re.finditer(pat, text_orig):
                name = match.group(2).strip()
                if len(name) > 3 and name not in people:
                    people.append(name)
                    all_people[name] += 1

        # Ticker extraction (uppercase 2-5 chars, filter common words)
        common = {"THE", "FOR", "AND", "BUT", "NOT", "FROM", "WITH", "THIS", "THAT",
                  "HAVE", "WILL", "BEEN", "THEY", "WERE", "SAID", "HAS", "ITS", "NEW",
                  "ARE", "WAS", "WHO", "HOW", "WHY", "ALL", "CAN", "MAY", "NOW", "SEC",
                  "GDP", "CEO", "CFO", "IPO", "ETF", "GDP", "CPI", "PMI"}
        for m in re.finditer(r'\b[A-Z]{2,5}\b', text_orig):
            t = m.group()
            if t not in common:
                tickers.append(t)
                all_tickers[t] += 1

        # Deduplicate
        countries = list({c["code"]: c for c in countries}.values())[:5]
        orgs = list(dict.fromkeys(orgs))[:5]
        people = list(dict.fromkeys(people))[:5]
        tickers = list(dict.fromkeys(tickers))[:5]

        per_article.append({
            "id": article.get("id", ""),
            "countries": countries,
            "organizations": orgs,
            "people": people,
            "tickers": tickers,
        })

    return {
        "success": True,
        "entities": per_article,
        "top_countries": [{"code": c, "count": n} for c, n in all_countries.most_common(10)],
        "top_organizations": [{"name": o, "count": n} for o, n in all_orgs.most_common(10)],
        "top_people": [{"name": p, "count": n} for p, n in all_people.most_common(10)],
        "top_tickers": [{"symbol": t, "count": n} for t, n in all_tickers.most_common(10)],
    }


def cluster_semantic(headlines_json):
    """Cluster headlines by TF-IDF cosine similarity."""
    try:
        articles = json.loads(headlines_json) if isinstance(headlines_json, str) else headlines_json
    except json.JSONDecodeError:
        return {"success": False, "error": "Invalid JSON"}

    if len(articles) < 2:
        return {"success": True, "clusters": [], "method": "too_few_articles"}

    # Tokenize and build TF-IDF manually (no sklearn dependency required)
    # Synonym map: normalize common abbreviations/variants to a canonical form
    SYNONYMS = {
        "fed": "federal_reserve", "federal": "federal_reserve", "reserve": "federal_reserve",
        "ecb": "european_central_bank",
        "boj": "bank_of_japan",
        "boe": "bank_of_england",
        "pboc": "peoples_bank_china",
        "nato": "nato_alliance",
        "gop": "republican",
        "kyiv": "ukraine_capital", "kiev": "ukraine_capital",
        "sanctioned": "sanction", "sanctions": "sanction",
        "rates": "interest_rate", "rate": "interest_rate",
        "inflation": "inflation_cpi",
        "troops": "military_troops", "soldiers": "military_troops",
        "missiles": "missile", "strikes": "strike", "attacked": "attack", "attacks": "attack",
        "crashed": "crash", "crashes": "crash",
        "rallied": "rally", "rallies": "rally",
    }

    def tokenize(text):
        words = re.findall(r'[a-z]{3,}', text.lower())
        stop = {"the", "and", "for", "are", "but", "not", "you", "all", "can",
                "had", "her", "was", "one", "our", "out", "has", "its", "his",
                "how", "who", "oil", "new", "now", "old", "see", "way", "may",
                "day", "too", "any", "been", "have", "from", "with", "they",
                "will", "each", "make", "like", "been", "than", "them", "then",
                "what", "when", "that", "this", "said", "over", "into", "also",
                "more", "some", "very", "after", "about", "which", "their", "there"}
        return [SYNONYMS.get(w, w) for w in words if w not in stop]

    docs = []
    for a in articles:
        tokens = tokenize(a.get("headline", "") + " " + a.get("summary", ""))
        docs.append(tokens)

    # Build vocabulary
    vocab = {}
    df = Counter()
    for doc in docs:
        seen = set()
        for w in doc:
            if w not in vocab:
                vocab[w] = len(vocab)
            if w not in seen:
                df[w] += 1
                seen.add(w)

    n_docs = len(docs)
    if not vocab:
        return {"success": True, "clusters": [], "method": "empty_vocab"}

    # TF-IDF vectors
    def tfidf_vec(tokens):
        tf = Counter(tokens)
        vec = {}
        for w, count in tf.items():
            if w in vocab:
                idf = math.log((n_docs + 1) / (df.get(w, 0) + 1)) + 1
                vec[vocab[w]] = (count / max(len(tokens), 1)) * idf
        return vec

    vectors = [tfidf_vec(doc) for doc in docs]

    # Cosine similarity
    def cosine(a, b):
        keys = set(a.keys()) & set(b.keys())
        if not keys:
            return 0.0
        dot = sum(a[k] * b[k] for k in keys)
        norm_a = math.sqrt(sum(v * v for v in a.values()))
        norm_b = math.sqrt(sum(v * v for v in b.values()))
        if norm_a == 0 or norm_b == 0:
            return 0.0
        return dot / (norm_a * norm_b)

    # Single-pass clustering (threshold = 0.25 — catches more topically related articles)
    THRESHOLD = 0.25
    clusters = []
    assigned = set()

    for i in range(len(vectors)):
        if i in assigned:
            continue
        cluster = [i]
        assigned.add(i)
        for j in range(i + 1, len(vectors)):
            if j in assigned:
                continue
            if cosine(vectors[i], vectors[j]) >= THRESHOLD:
                cluster.append(j)
                assigned.add(j)
        clusters.append(cluster)

    result_clusters = []
    for indices in clusters:
        if len(indices) < 2:
            continue
        items = [{"id": articles[i].get("id", ""), "headline": articles[i].get("headline", "")} for i in indices]
        result_clusters.append({
            "primary": items[0],
            "items": items,
            "size": len(items),
        })

    result_clusters.sort(key=lambda c: c["size"], reverse=True)

    return {
        "success": True,
        "clusters": result_clusters[:20],
        "method": "tfidf_cosine",
        "total_articles": len(articles),
        "clustered_count": sum(c["size"] for c in result_clusters),
    }


def resolve_arg(arg):
    """If arg starts with '@', read content from that file path and delete it."""
    if arg and arg.startswith("@"):
        path = arg[1:]
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = f.read()
            try:
                import os
                os.remove(path)
            except Exception:
                pass
            return data
        except Exception as e:
            return arg  # fallback: return as-is
    return arg


def main(args=None):
    if args is None:
        args = sys.argv[1:]

    if len(args) < 2:
        print(json.dumps({"success": False, "error": "Usage: news_nlp.py <command> <json_data>"}))
        return

    command = args[0]
    data = resolve_arg(args[1])

    if command == "extract_entities":
        result = extract_entities(data)
    elif command == "cluster_semantic":
        result = cluster_semantic(data)
    else:
        result = {"success": False, "error": f"Unknown command: {command}"}

    print(json.dumps(result))


if __name__ == "__main__":
    main()
