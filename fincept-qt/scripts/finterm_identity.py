"""The contact identity finterm declares to public data services.

SEC EDGAR, FINRA, OpenStreetMap, Wikipedia and Crossref ask every client to
name a contact in its User-Agent (SEC refuses requests without one). That
contact is the USER's, so it is configured — Settings › Credentials ›
"Contact email" (FINCEPT_CONTACT_EMAIL, kept in the OS keyring and injected
by PythonRunner) — and never written into the source tree.
"""

import os


_BAD = set('()<>,;:"[]\\')


def valid_contact(e: str) -> bool:
    """local@domain.tld, printable ASCII, nothing that breaks the
    "finterm (<email>)" comment or the header (CR/LF). Mirrors the C++
    header_safe_email() in MarketDataService.cpp, so both sides send the same."""
    if not (5 <= len(e) <= 254) or e.count("@") != 1:
        return False
    if any(not (0x21 <= ord(c) <= 0x7E) or c in _BAD for c in e):
        return False
    local, domain = e.split("@")
    return bool(local) and "." in domain and not domain.startswith(".") and not domain.endswith(".")


def contact_email() -> str:
    """The configured contact, or "" when unset or not a plain address (an
    invalid value is not sent — it would be refused or break the header)."""
    e = os.environ.get("FINCEPT_CONTACT_EMAIL", "").strip()
    return e if valid_contact(e) else ""


def project_user_agent(product: str = "finterm") -> str:
    """The TOOL's identity, never the user's address — for every service the
    contact setting does not name (it is declared to SEC, FINRA and
    OpenStreetMap only): Wikimedia, IndexMundi / World Bank, CFTC, …"""
    return f"{product} (https://github.com/yumima/finterm)"


def user_agent(product: str = "finterm") -> str:
    """The user's configured contact — ONLY for SEC, FINRA and OpenStreetMap,
    which require one. Anything else uses project_user_agent()."""
    email = contact_email()
    if email:
        return f"{product} ({email})"
    # No personal contact configured: declare the project instead (the C++
    # side's sec_user_agent() does the same). SEC STOPPED accepting this in
    # 2026-10 (403 "Undeclared Automated Tool") — without a configured contact
    # its requests fail; the fix is the setting, never someone's personal
    # address baked into the source.
    return f"{product} (https://github.com/yumima/finterm)"
