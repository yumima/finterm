#!/usr/bin/env python3
"""Generate the credential catalogue from the scripts that actually read keys.

There were three hand-maintained lists of "the app's API keys" and all three
disagreed: SettingsScreen offered 7, PythonRunner injected and allow-listed
18, and the data scripts read 142. The consequence was not cosmetic — the
subprocess env strips any credential-shaped variable that is not on
PythonRunner's allow-list, so a user who exported EIA_API_KEY in their shell
had it deleted before the script ran, and the panel reported "API key not
configured" for a key they had correctly set.

The scripts are the ground truth: a key matters exactly when some script
reads it. This emits that set as a committed header, and a test re-runs the
scan and diffs, so adding a new data source without registering its key fails
the build instead of silently breaking at runtime.

Usage:
    python3 scripts/gen_credential_catalogue.py            # write the header
    python3 scripts/gen_credential_catalogue.py --check    # exit 1 if stale
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "core", "keys", "CredentialCatalogue.h")

# os.environ.get("X"), os.environ["X"], os.getenv("X")
PATTERN = re.compile(r"""os\.(?:environ\.get\(|environ\[|getenv\()\s*['"]([A-Z][A-Z0-9_]{3,})['"]""")

# Environment variables that are not credentials.
NOT_CREDENTIALS = {
    "APPDATA", "HOME", "PATH", "USER", "USERNAME", "TMPDIR", "TEMP", "TMP",
    "PYTHONPATH", "PYTHONHOME", "LOCALAPPDATA", "PROGRAMDATA", "LANG",
    "SYSTEMROOT", "COMSPEC", "SHELL", "TERM", "DISPLAY", "PWD",
    "VIRTUAL_ENV", "CONDA_PREFIX", "SSL_CERT_FILE", "REQUESTS_CA_BUNDLE",
    "NO_PROXY", "HTTP_PROXY", "HTTPS_PROXY", "CI",
    # finterm's own data directory, used by scripts that keep a local index
    # (the 13F SQLite store). A path, not a secret.
    "FINTERM_DATA_DIR",
    # The per-user data and cache homes (scripts/finterm_paths.py). Paths too.
    "FINTERM_USER_DIR", "FINTERM_USER_DATA_DIR", "FINTERM_CACHE_DIR",
    # Logging switches some scripts read; configuration, not credentials.
    "LOGURU_AUTOINIT", "LOGURU_LEVEL",
    # Which vendor answers prices (Settings › Data Sources); a choice, not a key.
    "FINTERM_PRICE_SOURCE",
    # The per-launch token the host sets directly on its data daemon's process
    # for the loopback-TCP transport (PythonWorker). Not a user credential, and
    # not passed through the allow-list: it must never appear in Settings.
    "FINTERM_DAEMON_TOKEN",
}

# Human labels for the providers users are most likely to configure. Anything
# absent falls back to a title-cased form of the key, which is serviceable.
LABELS = {
    "BEA_API_KEY": "BEA (Bureau of Economic Analysis)",
    "BLS_API_KEY": "BLS (Bureau of Labor Statistics)",
    "CENSUS_API_KEY": "US Census Bureau",
    "CONGRESS_GOV_API_KEY": "Congress.gov (Gov Data)",
    # Optional. Without it the 13F symbol map takes ~95 minutes; with it, ~2.
    "OPENFIGI_API_KEY": "OpenFIGI (13F symbol mapping — optional)",
    "DATABENTO_API_KEY": "Databento",
    "EIA_API_KEY": "EIA (Energy Information Administration)",
    "FINANCIAL_DATASETS_API_KEY": "Financial Datasets (financialdatasets.ai)",
    "FINTERM_CONTACT_EMAIL": "Contact email (declared to SEC, FINRA, OpenStreetMap)",
    "FINNHUB_API_KEY": "Finnhub",
    "FRED_API_KEY": "FRED (Federal Reserve)",
    "POLYGON_API_KEY": "Polygon.io / Massive",
    "ALPACA_API_KEY_ID": "Alpaca market data — key ID",
    "ALPACA_API_SECRET_KEY": "Alpaca market data — secret key",
    "FMP_API_KEY": "Financial Modeling Prep",
    "GITHUB_ACCESS_TOKEN": "GitHub (agents' GitHub tools)",
    "OPENBB_PAT": "OpenBB personal access token (agents' OpenBB tools)",
    "TIINGO_API_KEY": "Tiingo",
}

# Keys used by C++ services that no Python script reads. They still belong in
# the catalogue: the user configures them, and the allow-list must not strip
# them from an inherited shell environment.
CPP_ONLY = [
    # MCP financial-datasets tools (FinancialDatasetsTools.cpp).
    "FINANCIAL_DATASETS_API_KEY",
]

# Keys read by libraries our scripts call, not by our own os.environ lookups,
# so the scan cannot see them. Each entry names the library call that reads
# it; re-check on a library upgrade. Without an entry the key is neither
# offered in Settings nor injected, and a credential-shaped name (a suffix in
# PythonRunner's strip list) is deleted from an inherited shell environment.
LIBRARY_READ = [
    # agno.tools.github.GithubTools: getenv("GITHUB_ACCESS_TOKEN") — the
    # agents' "github" toolkit (finagent_core tools_registry). _ACCESS_TOKEN is
    # a stripped suffix, so a token exported in the shell never reached it.
    "GITHUB_ACCESS_TOKEN",
    # agno.tools.openbb.OpenBBTools: getenv("OPENBB_PAT") logs in to OpenBB —
    # the agents' "openbb" toolkit.
    "OPENBB_PAT",
]


def scan():
    keys = set()
    for name in sorted(os.listdir(HERE)):
        if not name.endswith(".py"):
            continue
        path = os.path.join(HERE, name)
        try:
            with open(path, encoding="utf-8") as fh:
                for m in PATTERN.finditer(fh.read()):
                    keys.add(m.group(1))
        except OSError:
            continue
    keys |= set(CPP_ONLY) | set(LIBRARY_READ)
    return sorted(k for k in keys if k not in NOT_CREDENTIALS)


# Settings › API Keys groups the catalogue so a user can find a provider by
# what it is for. First matching group wins; anything unmatched falls into
# "Other data sources". Order here is the display order.
GROUPS = [
    ("Your identity", {
        "FINTERM_CONTACT_EMAIL",
    }),
    ("Market data", {
        "ALPACA_API_KEY_ID", "ALPACA_API_SECRET_KEY", "DATABENTO_API_KEY",
        "FINANCIAL_DATASETS_API_KEY", "FINNHUB_API_KEY", "FMP_API_KEY", "OPENFIGI_API_KEY",
        "POLYGON_API_KEY", "TIINGO_API_KEY",
    }),
    ("AI agent tools", {
        "GITHUB_ACCESS_TOKEN", "OPENBB_PAT",
    }),
]
# Everything else is economics / government / international statistics.
DEFAULT_GROUP = "Economics & government"

# Values that are identifiers or preferences, not secrets: shown in clear.
NON_SECRET_SUFFIXES = ("_EMAIL", "_USERNAME", "_LANGUAGE", "_CLIENT_ID", "_APP_ID")


def group_for(key):
    for name, members in GROUPS:
        if key in members:
            return name
    return DEFAULT_GROUP


def is_secret(key):
    return not key.endswith(NON_SECRET_SUFFIXES)


def label_for(key):
    if key in LABELS:
        return LABELS[key]
    stem = key
    for suffix in ("_API_KEY", "_APP_TOKEN", "_ACCESS_TOKEN", "_SECRET_KEY",
                   "_SECRET", "_TOKEN", "_KEY", "_EMAIL", "_APP_ID"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break
    return " ".join(w.capitalize() for w in stem.split("_"))


def render(keys):
    lines = [
        "// GENERATED FILE — do not edit by hand.",
        "//",
        "// Regenerate:  python3 scripts/gen_credential_catalogue.py",
        "// Verified by: tests/scripts/test_credential_catalogue.py (ctest)",
        "//",
        "// Every credential the app can use, derived from the scripts that read",
        "// them. This is the single source of truth for three things that used to",
        "// be three separate hand-maintained lists, all disagreeing:",
        "//   • which keys SettingsScreen offers the user,",
        "//   • which keys PythonRunner injects from SecureStorage,",
        "//   • which keys survive the subprocess credential strip.",
        "//",
        "// That last one made the drift a real bug rather than untidiness: a",
        "// credential-shaped variable missing from the allow-list is DELETED from",
        "// the child environment, so a key the user had correctly exported was",
        "// reported by the panel as \"not configured\".",
        "",
        "// Generated, and --check compares it byte-for-byte: keep the formatter off.",
        "// clang-format off",
        "",
        "#pragma once",
        "",
        "#include <QString>",
        "#include <QStringList>",
        "#include <QVector>",
        "",
        "namespace finterm::keys {",
        "",
        "struct CredentialDef {",
        "    const char* env_name;  ///< e.g. \"FRED_API_KEY\"",
        "    const char* label;     ///< human name shown in Settings",
        "    const char* group;     ///< Settings › API Keys section",
        "    bool secret;           ///< false: an id/email/preference, shown in clear",
        "};",
        "",
        "/// Section order for Settings › API Keys.",
        "inline const QStringList& groups() {",
        f"    static const QStringList kGroups = {{{', '.join(chr(34)+g+chr(34) for g in [n for n, _ in GROUPS] + [DEFAULT_GROUP])}}};",
        "    return kGroups;",
        "}",
        "",
        f"// {len(keys)} credentials.",
        "inline const QVector<CredentialDef>& catalogue() {",
        "    static const QVector<CredentialDef> kAll = {",
    ]
    for k in keys:
        secret = "true" if is_secret(k) else "false"
        lines.append(f'        {{"{k}", "{label_for(k)}", "{group_for(k)}", {secret}}},')
    lines += [
        "    };",
        "    return kAll;",
        "}",
        "",
        "/// Env-var names only — the allow-list for credential injection and for",
        "/// stripping unmanaged secrets out of a child process environment.",
        "inline const QStringList& env_names() {",
        "    static const QStringList kNames = [] {",
        "        QStringList out;",
        "        out.reserve(catalogue().size());",
        "        for (const auto& c : catalogue())",
        "            out << QString::fromLatin1(c.env_name);",
        "        return out;",
        "    }();",
        "    return kNames;",
        "}",
        "",
        "} // namespace finterm::keys",
        "",
        "// clang-format on",
        "",
    ]
    return "\n".join(lines)


def main():
    keys = scan()
    text = render(keys)
    out = os.path.abspath(OUT)
    if "--check" in sys.argv:
        try:
            with open(out, encoding="utf-8") as fh:
                current = fh.read()
        except OSError:
            print(f"FAIL: {out} is missing — run gen_credential_catalogue.py")
            return 1
        if current != text:
            print("FAIL: CredentialCatalogue.h is stale.\n"
                  "      A script reads a credential the catalogue does not list (or vice\n"
                  "      versa). An unlisted key is STRIPPED from the subprocess env, so the\n"
                  "      data source silently reports 'not configured'.\n"
                  "      Fix: python3 scripts/gen_credential_catalogue.py")
            return 1
        print(f"OK: catalogue is in sync ({len(keys)} credentials)")
        return 0

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {out} ({len(keys)} credentials)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
