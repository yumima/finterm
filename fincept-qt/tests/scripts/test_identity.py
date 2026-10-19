#!/usr/bin/env python3
"""The contact identity finterm declares to SEC / FINRA / OSM.

SEC refuses requests without a declared contact (403 "Undeclared Automated
Tool", since 2026-10), and the address goes into an HTTP header inside a
"finterm (<email>)" comment — so only a plain local@domain.tld in printable
ASCII is sent. The C++ side (MarketDataService header_safe_email) applies the
same rule; the cases below are the ones its review named.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import finterm_identity as fi

fails = []
for e, want in [("me@example.com", True), ("a.b+c@sub.example.co", True),
                ("me@", False), ("@example.com", False), ("me@example", False),
                ("me@.com", False), ("me@example.com.", False),
                ("a)b@c.com", False), ("a(b@c.com", False), ("a b@c.com", False),
                ("josé@example.de", False), ("me@example.com\r\nX: y", False), ("", False)]:
    if fi.valid_contact(e) != want:
        fails.append(f"valid_contact({e!r}) should be {want}")

os.environ["FINCEPT_CONTACT_EMAIL"] = "  me@example.com  "
if fi.user_agent() != "finterm (me@example.com)":
    fails.append(f"configured contact -> {fi.user_agent()!r}")
os.environ["FINCEPT_CONTACT_EMAIL"] = "me@"
if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
    fails.append(f"an invalid contact must not be sent: {fi.user_agent()!r}")
os.environ.pop("FINCEPT_CONTACT_EMAIL")
if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
    fails.append("unset -> the project identity")

# The contact is declared to SEC, FINRA and OpenStreetMap ONLY (the setting
# says so). Every other caller must use project_user_agent(): a scan of the
# scripts for user_agent() outside these files is a leak of the address.
import re
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts")
DECLARED = {  # host each file talks to
    "sec_form_d_data.py", "sec_data.py", "sec_s1_pipeline.py", "sec_form4_bulk.py", "sec_13f_bulk.py",
    "sec_xbrl_data.py", "sec_nport_marks.py", "sec_ownership_data.py",
    "finra_short_volume.py", "finra_short_interest.py",          # FINRA
    "nominatim_data.py", "overpass_api_data.py",                 # OpenStreetMap
    "yfinance_data.py",                                          # its EDGAR document fetch
    os.path.join("mcp", "edgar", "base.py"),                     # EDGAR
    os.path.join("Analytics", "corporateFinance", "deal_database", "deal_scanner.py"),  # EDGAR
}
for root, _, files in os.walk(SCRIPTS):
    for fn in files:
        if not fn.endswith(".py") or fn == "finterm_identity.py":
            continue
        path = os.path.join(root, fn)
        rel = os.path.relpath(path, SCRIPTS)
        src = open(path, encoding="utf-8", errors="replace").read()
        # any import of the address-bearing names — aliased or not — counts;
        # project_user_agent is the safe one
        imports = re.findall(r"from\s+finterm_identity\s+import\s+([^\n]+)", src)
        names = {n.strip().split(" as ")[0].strip("() ") for line in imports for n in line.split(",")}
        if re.search(r"import\s+finterm_identity", src):
            names.add("<module>")  # finterm_identity.user_agent / .contact_email reachable
        leaky = names & {"user_agent", "contact_email", "<module>"}
        if leaky and rel not in DECLARED:
            fails.append(f"{rel} can send the user's contact ({', '.join(sorted(leaky))}) "
                         f"to a host the setting does not name")

for f in fails:
    print("FAIL:", f)
print("identity: all checks passed" if not fails else f"{len(fails)} failure(s)")
sys.exit(1 if fails else 0)
