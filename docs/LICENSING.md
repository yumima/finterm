# Licensing

**finterm has no separate commercial license.** It is distributed exclusively under **AGPL-3.0-or-later**.

That means:

- **Free for any use** (personal, educational, commercial, internal-business, embedded in your own product) as long as you comply with the AGPL.
- **No SaaS / network-service exception.** If you run a modified version of finterm as a network service (web app, hosted dashboard, API behind a frontend), the AGPL requires you to make your modified source available to your users (sections 6 and 13 of the license).
- **No paid tiers, no enterprise edition, no priority-support contract.**

Part of the code is derived from a third-party AGPL/MIT project; its copyright notice is kept in [`NOTICE`](../NOTICE), as both licenses require. Because of that, finterm as a whole can only be distributed under the AGPL — a closed-source build would need the third-party portions removed or separately licensed.

Earlier copies of `LICENSE` carried a "no commercial use" rider. Under AGPL section 7 such further restrictions may be removed, and they have been; the binding terms are the plain AGPL text in `LICENSE`.

## Data sources

The market-data APIs the app calls (Yahoo Finance, Stooq, akshare, FRED, etc.) have their **own** terms of service. You're responsible for complying with each provider's rate limits and acceptable-use rules — that's separate from finterm's own license.

## Read the license

- [`LICENSE`](../LICENSE) — full AGPL-3.0 text (binding)
- [`NOTICE`](../NOTICE) — copyright and third-party attribution
- [GNU AGPL-3.0 official text](https://www.gnu.org/licenses/agpl-3.0.html)

For the practical TL;DR of what AGPL-3.0 actually requires, the in-app **Terms** screen has a plain-English summary.
