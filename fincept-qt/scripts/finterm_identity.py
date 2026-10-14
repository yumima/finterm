"""The contact identity finterm declares to public data services.

SEC EDGAR, FINRA, OpenStreetMap, Wikipedia and Crossref ask every client to
name a contact in its User-Agent (SEC refuses requests without one). That
contact is the USER's, so it is configured — Settings › Credentials ›
"Contact email" (FINCEPT_CONTACT_EMAIL, kept in the OS keyring and injected
by PythonRunner) — and never written into the source tree.
"""

import os


def contact_email() -> str:
    return os.environ.get("FINCEPT_CONTACT_EMAIL", "").strip()


def user_agent(product: str = "finterm") -> str:
    email = contact_email()
    if email:
        return f"{product} ({email})"
    # No personal contact configured: declare the project instead — the same
    # identity the C++ side sends (MarketDataService kSecUserAgent), which SEC
    # accepts. Never someone's personal address baked into the source.
    return f"{product} (https://github.com/yumima/finterm)"
