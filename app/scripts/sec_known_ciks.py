"""Shared curated SEC CIKs for marquee late-stage private companies.

Single source of truth imported by both sec_form_d_data.py (Form D universe)
and sec_s1_pipeline.py (S-1/F-1 pipeline). Curated CIKs are force-fetched from
the EDGAR submissions API so well-known names always surface regardless of the
noisy recent-filings sweep or its hit cap.

Only VERIFIED CIKs belong here. Names without their own operating-company CIK
(Anthropic, OpenAI, Databricks, …) are picked up by the N-PORT mutual-fund-mark
layer and the curated valuation seed instead.
"""

# Padded 10-digit CIKs.
KNOWN_PRIVATE_CIKS = [
    "0001181412",  # Space Exploration Technologies (SpaceX) — VERIFIED.
                   #   Form D 2020–2022 (~$8.2B/8 rounds); public S-1 filed
                   #   2026-05-20 (ticker SPCX), S-1/A 06-01 & 06-03.
    "0001691342",  # Stripe, Inc. — VERIFIED (files Form D). (Note: 0001646180,
                   #   the prior value, is IHS Holding Inc., an unrelated public
                   #   company — do not use it for Stripe.)
]
