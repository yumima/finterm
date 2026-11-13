"""
Derivatives Pricing Bridge Script
==================================
CLI bridge for the QT terminal to invoke derivatives pricing calculations.
Wraps the Analytics/derivatives modules and returns JSON results.

Usage:
    python derivatives_pricing.py <command> [--param value ...]

Commands:
    bond_price          - Calculate bond price from YTM
    bond_ytm            - Calculate YTM from clean price
    option_price        - Black-Scholes option pricing + Greeks
    implied_vol         - Calculate implied volatility
    fx_option_price     - FX vanilla option pricing (Garman-Kohlhagen)
    swap_value          - Interest rate swap valuation
    cds_value           - Credit default swap valuation
    forward_price       - Forward/futures pricing
"""

import sys
import json
import argparse
import math
from datetime import datetime, timedelta

try:
    import numpy as np
    from scipy.stats import norm
    from scipy.optimize import brentq
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False


def black_scholes_price(S, K, T, r, sigma, q=0.0, option_type="call"):
    """Black-Scholes-Merton option pricing"""
    if T <= 0:
        if option_type == "call":
            return max(0.0, S - K)
        else:
            return max(0.0, K - S)

    d1 = (math.log(S / K) + (r - q + 0.5 * sigma ** 2) * T) / (sigma * math.sqrt(T))
    d2 = d1 - sigma * math.sqrt(T)

    if option_type == "call":
        price = S * math.exp(-q * T) * norm.cdf(d1) - K * math.exp(-r * T) * norm.cdf(d2)
    else:
        price = K * math.exp(-r * T) * norm.cdf(-d2) - S * math.exp(-q * T) * norm.cdf(-d1)

    return price


def black_scholes_greeks(S, K, T, r, sigma, q=0.0, option_type="call"):
    """Calculate all Greeks for BSM model"""
    if T <= 0:
        return {"delta": 0, "gamma": 0, "theta": 0, "vega": 0, "rho": 0}

    d1 = (math.log(S / K) + (r - q + 0.5 * sigma ** 2) * T) / (sigma * math.sqrt(T))
    d2 = d1 - sigma * math.sqrt(T)

    sqrt_T = math.sqrt(T)
    pdf_d1 = norm.pdf(d1)
    exp_qT = math.exp(-q * T)
    exp_rT = math.exp(-r * T)

    # Gamma (same for call and put)
    gamma = (pdf_d1 * exp_qT) / (S * sigma * sqrt_T)

    # Vega (same for call and put) — per 1% move
    vega = S * exp_qT * pdf_d1 * sqrt_T * 0.01

    if option_type == "call":
        delta = exp_qT * norm.cdf(d1)
        theta = (-(S * pdf_d1 * sigma * exp_qT) / (2 * sqrt_T)
                 - r * K * exp_rT * norm.cdf(d2)
                 + q * S * exp_qT * norm.cdf(d1)) / 365.0
        rho = K * T * exp_rT * norm.cdf(d2) * 0.01
    else:
        delta = exp_qT * (norm.cdf(d1) - 1)
        theta = (-(S * pdf_d1 * sigma * exp_qT) / (2 * sqrt_T)
                 + r * K * exp_rT * norm.cdf(-d2)
                 - q * S * exp_qT * norm.cdf(-d1)) / 365.0
        rho = -K * T * exp_rT * norm.cdf(-d2) * 0.01

    return {
        "delta": round(delta, 6),
        "gamma": round(gamma, 6),
        "theta": round(theta, 6),
        "vega": round(vega, 6),
        "rho": round(rho, 6),
    }


def implied_volatility(S, K, T, r, market_price, q=0.0, option_type="call"):
    """Calculate implied volatility using Brent's method"""
    if T <= 0:
        return 0.0

    def objective(sigma):
        return black_scholes_price(S, K, T, r, sigma, q, option_type) - market_price

    try:
        iv = brentq(objective, 0.001, 10.0, xtol=1e-8, maxiter=200)
        return iv
    except (ValueError, RuntimeError):
        return -1.0


def garman_kohlhagen_price(S, K, T, r_d, r_f, sigma, option_type="call", notional=1.0):
    """Garman-Kohlhagen FX option pricing"""
    if T <= 0:
        if option_type == "call":
            return max(0.0, S - K) * notional
        else:
            return max(0.0, K - S) * notional

    d1 = (math.log(S / K) + (r_d - r_f + 0.5 * sigma ** 2) * T) / (sigma * math.sqrt(T))
    d2 = d1 - sigma * math.sqrt(T)

    if option_type == "call":
        price = S * math.exp(-r_f * T) * norm.cdf(d1) - K * math.exp(-r_d * T) * norm.cdf(d2)
    else:
        price = K * math.exp(-r_d * T) * norm.cdf(-d2) - S * math.exp(-r_f * T) * norm.cdf(-d1)

    return price * notional


def _add_months(d, months):
    """Shift a date by whole months, clamping the day to the target month's end."""
    import calendar
    m = d.month - 1 + months
    y = d.year + m // 12
    m = m % 12 + 1
    day = min(d.day, calendar.monthrange(y, m)[1])
    return d.replace(year=y, month=m, day=day)


def bond_price_from_ytm(issue_date, settlement_date, maturity_date, coupon_rate, ytm, freq):
    """Calculate bond clean/dirty price from YTM (street convention).

    The coupon schedule is generated backwards from maturity in 12/freq-month
    steps. The dirty price discounts every remaining cash flow with a
    fractional first period w = days(settle, next) / days(prev, next)
    (Actual/Actual ICMA); accrued = coupon * days(accrual start, settle) /
    days(prev, next), where accrual starts at max(prev coupon, issue date);
    clean = dirty - accrued.
    """
    coupon_rate_dec = coupon_rate / 100.0
    ytm_dec = ytm / 100.0
    freq = int(freq)
    if freq <= 0 or 12 % freq != 0:
        return {"error": "Coupon frequency must be 1, 2, 3, 4, 6 or 12"}

    settle = datetime.strptime(settlement_date, "%Y-%m-%d")
    maturity = datetime.strptime(maturity_date, "%Y-%m-%d")
    issue = datetime.strptime(issue_date, "%Y-%m-%d")

    if maturity <= settle:
        return {"error": "Settlement must be before maturity"}
    if settle < issue:
        return {"error": "Settlement must be on or after issue date"}

    step = 12 // freq
    # Remaining coupon dates (strictly after settlement), built back from maturity.
    k = 0
    coupon_dates = []
    d = maturity
    while d > settle:
        coupon_dates.append(d)
        k += 1
        d = _add_months(maturity, -step * k)
    prev_coupon = d  # last scheduled coupon date on/before settlement
    coupon_dates.reverse()
    next_coupon = coupon_dates[0]
    n_periods = len(coupon_dates)

    coupon = coupon_rate_dec / freq * 100.0  # per 100 face
    y_per = ytm_dec / freq

    period_days = (next_coupon - prev_coupon).days
    w = (next_coupon - settle).days / period_days  # fraction of first period remaining
    accrual_start = max(prev_coupon, issue)
    accrued = coupon * (settle - accrual_start).days / period_days

    # Cash flows at exponents (w + i) for i = 0..n-1, in periods.
    cfs = [coupon] * n_periods
    cfs[-1] += 100.0
    exps = [w + i for i in range(n_periods)]
    disc = [(1 + y_per) ** (-e) for e in exps]

    dirty_price = sum(cf * df for cf, df in zip(cfs, disc))
    clean_price = dirty_price - accrued

    # Macaulay duration (years) on the same fractional schedule
    mac_duration = sum((e / freq) * cf * df for e, cf, df in zip(exps, cfs, disc)) / dirty_price

    # Convexity (years^2), periodic-compounding convention
    conv_sum = sum((e / freq) * ((e + 1) / freq) * cf * df for e, cf, df in zip(exps, cfs, disc))
    convexity = conv_sum / (dirty_price * (1 + y_per) ** 2)

    return {
        "clean_price": round(clean_price, 4),
        "dirty_price": round(dirty_price, 4),
        "accrued_interest": round(accrued, 4),
        "duration": round(mac_duration, 4),
        "convexity": round(convexity, 4),
        "ytm": round(ytm, 4),
        "remaining_coupons": n_periods,
    }


def bond_ytm_from_price(issue_date, settlement_date, maturity_date, coupon_rate, clean_price, freq):
    """Calculate YTM from clean price using Newton's method"""

    def price_at_ytm(y):
        res = bond_price_from_ytm(issue_date, settlement_date, maturity_date, coupon_rate, y, freq)
        if "error" in res:
            return 999
        return res["clean_price"] - clean_price

    try:
        ytm_result = brentq(price_at_ytm, -5.0, 100.0, xtol=1e-8, maxiter=200)
        return {"ytm": round(ytm_result, 6)}
    except (ValueError, RuntimeError):
        return {"error": "Could not converge on YTM"}


def swap_value(effective_date, maturity_date, fixed_rate, freq, notional, discount_rate):
    """Simple interest rate swap valuation (fixed vs floating)"""
    fixed_rate_dec = fixed_rate / 100.0
    disc_rate_dec = discount_rate / 100.0

    eff = datetime.strptime(effective_date, "%Y-%m-%d")
    mat = datetime.strptime(maturity_date, "%Y-%m-%d")

    T = (mat - eff).days / 365.25
    if T <= 0:
        return {"error": "Maturity must be after effective date"}

    n_periods = max(1, int(T * freq))
    dt = 1.0 / freq

    # Fixed leg PV
    fixed_pv = 0.0
    for i in range(1, n_periods + 1):
        t_i = i * dt
        df = math.exp(-disc_rate_dec * t_i)
        fixed_pv += fixed_rate_dec * dt * notional * df

    # Floating leg PV (par at inception)
    float_pv = notional * (1 - math.exp(-disc_rate_dec * T))

    # Swap value = floating - fixed (for receiver of floating)
    swap_val = float_pv - fixed_pv

    # Par swap rate
    annuity = 0.0
    for i in range(1, n_periods + 1):
        t_i = i * dt
        annuity += dt * math.exp(-disc_rate_dec * t_i)
    par_rate = (1 - math.exp(-disc_rate_dec * T)) / annuity if annuity > 0 else 0

    return {
        "swap_value": round(swap_val, 2),
        "fixed_leg_pv": round(fixed_pv, 2),
        "floating_leg_pv": round(float_pv, 2),
        "par_swap_rate": round(par_rate * 100, 4),
        "notional": notional,
        "tenor_years": round(T, 2),
    }


def cds_value(valuation_date, maturity_date, recovery_rate, notional, spread_bps, discount_rate):
    """Simple CDS valuation. discount_rate is the continuously-compounded risk-free rate in %."""
    if discount_rate is None:
        return {"error": "Discount (risk-free) rate is required"}
    rec_rate = recovery_rate / 100.0
    spread = spread_bps / 10000.0

    val = datetime.strptime(valuation_date, "%Y-%m-%d")
    mat = datetime.strptime(maturity_date, "%Y-%m-%d")

    T = (mat - val).days / 365.25
    if T <= 0:
        return {"error": "Maturity must be after valuation date"}

    # Simple hazard rate from spread: h = spread / (1 - R)
    lgd = 1 - rec_rate
    if lgd <= 0:
        return {"error": "LGD must be positive (recovery < 100%)"}

    hazard_rate = spread / lgd

    r = discount_rate / 100.0

    # Premium leg PV (quarterly payments)
    premium_pv = 0.0
    n_quarters = max(1, int(T * 4))
    for i in range(1, n_quarters + 1):
        t_i = i * 0.25
        if t_i > T:
            break
        surv = math.exp(-hazard_rate * t_i)
        df = math.exp(-r * t_i)
        premium_pv += spread * 0.25 * notional * surv * df

    # Protection leg PV
    protection_pv = 0.0
    n_steps = max(1, int(T * 12))
    dt = T / n_steps
    for i in range(1, n_steps + 1):
        t_i = i * dt
        surv_prev = math.exp(-hazard_rate * (t_i - dt))
        surv_curr = math.exp(-hazard_rate * t_i)
        default_prob = surv_prev - surv_curr
        df = math.exp(-r * t_i)
        protection_pv += lgd * notional * default_prob * df

    # Upfront value
    upfront = protection_pv - premium_pv

    # Breakeven spread
    annuity = 0.0
    for i in range(1, n_quarters + 1):
        t_i = i * 0.25
        if t_i > T:
            break
        surv = math.exp(-hazard_rate * t_i)
        df = math.exp(-r * t_i)
        annuity += 0.25 * notional * surv * df

    breakeven_spread = (protection_pv / annuity * 10000) if annuity > 0 else 0

    # Survival probability
    survival_prob = math.exp(-hazard_rate * T)

    return {
        "upfront_value": round(upfront, 2),
        "premium_leg_pv": round(premium_pv, 2),
        "protection_leg_pv": round(protection_pv, 2),
        "breakeven_spread_bps": round(breakeven_spread, 2),
        "hazard_rate": round(hazard_rate * 100, 4),
        "survival_probability": round(survival_prob * 100, 2),
        "notional": notional,
    }


def forward_price(spot, r, T, q=0.0, storage=0.0, convenience=0.0):
    """Calculate forward/futures price using cost-of-carry model"""
    carry = r - q + storage - convenience
    fwd = spot * math.exp(carry * T)
    return {
        "forward_price": round(fwd, 4),
        "spot_price": spot,
        "carry_rate": round(carry * 100, 4),
        "time_to_expiry": T,
    }


def main():
    parser = argparse.ArgumentParser(description="Derivatives Pricing Engine")
    subparsers = parser.add_subparsers(dest="command")

    # Bond price
    bp = subparsers.add_parser("bond_price")
    bp.add_argument("--issue-date", required=True)
    bp.add_argument("--settlement-date", required=True)
    bp.add_argument("--maturity-date", required=True)
    bp.add_argument("--coupon-rate", type=float, required=True)
    bp.add_argument("--ytm", type=float, required=True)
    bp.add_argument("--freq", type=int, default=2)

    # Bond YTM
    by = subparsers.add_parser("bond_ytm")
    by.add_argument("--issue-date", required=True)
    by.add_argument("--settlement-date", required=True)
    by.add_argument("--maturity-date", required=True)
    by.add_argument("--coupon-rate", type=float, required=True)
    by.add_argument("--clean-price", type=float, required=True)
    by.add_argument("--freq", type=int, default=2)

    # Option price
    op = subparsers.add_parser("option_price")
    op.add_argument("--spot", type=float, required=True)
    op.add_argument("--strike", type=float, required=True)
    op.add_argument("--time", type=float, required=True, help="Time to expiry in years")
    op.add_argument("--rate", type=float, required=True, help="Risk-free rate as percentage")
    op.add_argument("--vol", type=float, required=True, help="Volatility as percentage")
    op.add_argument("--div-yield", type=float, default=0.0, help="Dividend yield as percentage")
    op.add_argument("--type", choices=["call", "put"], default="call")

    # Implied vol
    iv = subparsers.add_parser("implied_vol")
    iv.add_argument("--spot", type=float, required=True)
    iv.add_argument("--strike", type=float, required=True)
    iv.add_argument("--time", type=float, required=True)
    iv.add_argument("--rate", type=float, required=True)
    iv.add_argument("--market-price", type=float, required=True)
    iv.add_argument("--div-yield", type=float, default=0.0)
    iv.add_argument("--type", choices=["call", "put"], default="call")

    # FX option
    fx = subparsers.add_parser("fx_option_price")
    fx.add_argument("--spot", type=float, required=True)
    fx.add_argument("--strike", type=float, required=True)
    fx.add_argument("--time", type=float, required=True)
    fx.add_argument("--domestic-rate", type=float, required=True)
    fx.add_argument("--foreign-rate", type=float, required=True)
    fx.add_argument("--vol", type=float, required=True)
    fx.add_argument("--type", choices=["call", "put"], default="call")
    fx.add_argument("--notional", type=float, default=1000000)

    # Swap
    sw = subparsers.add_parser("swap_value")
    sw.add_argument("--effective-date", required=True)
    sw.add_argument("--maturity-date", required=True)
    sw.add_argument("--fixed-rate", type=float, required=True)
    sw.add_argument("--freq", type=int, default=2)
    sw.add_argument("--notional", type=float, default=1000000)
    sw.add_argument("--discount-rate", type=float, required=True)

    # CDS
    cd = subparsers.add_parser("cds_value")
    cd.add_argument("--valuation-date", required=True)
    cd.add_argument("--maturity-date", required=True)
    cd.add_argument("--recovery-rate", type=float, required=True)
    cd.add_argument("--notional", type=float, default=10000000)
    cd.add_argument("--spread-bps", type=float, required=True)
    cd.add_argument("--discount-rate", type=float, required=True)

    # Forward
    fw = subparsers.add_parser("forward_price")
    fw.add_argument("--spot", type=float, required=True)
    fw.add_argument("--rate", type=float, required=True)
    fw.add_argument("--time", type=float, required=True)
    fw.add_argument("--div-yield", type=float, default=0.0)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    if not HAS_SCIPY:
        print(json.dumps({"error": "scipy not installed. Run: pip install scipy numpy"}))
        sys.exit(1)

    try:
        if args.command == "bond_price":
            result = bond_price_from_ytm(
                args.issue_date, args.settlement_date, args.maturity_date,
                args.coupon_rate, args.ytm, args.freq
            )
        elif args.command == "bond_ytm":
            result = bond_ytm_from_price(
                args.issue_date, args.settlement_date, args.maturity_date,
                args.coupon_rate, args.clean_price, args.freq
            )
        elif args.command == "option_price":
            S, K, T = args.spot, args.strike, args.time
            r, sigma, q = args.rate / 100, args.vol / 100, args.div_yield / 100
            price = black_scholes_price(S, K, T, r, sigma, q, args.type)
            greeks = black_scholes_greeks(S, K, T, r, sigma, q, args.type)
            result = {"price": round(price, 6), "greeks": greeks}
        elif args.command == "implied_vol":
            S, K, T = args.spot, args.strike, args.time
            r, q = args.rate / 100, args.div_yield / 100
            iv_val = implied_volatility(S, K, T, r, args.market_price, q, args.type)
            if iv_val < 0:
                result = {"error": "Could not converge on implied volatility"}
            else:
                result = {"implied_volatility": round(iv_val * 100, 4)}
        elif args.command == "fx_option_price":
            S, K, T = args.spot, args.strike, args.time
            r_d, r_f = args.domestic_rate / 100, args.foreign_rate / 100
            sigma = args.vol / 100
            price = garman_kohlhagen_price(S, K, T, r_d, r_f, sigma, args.type, args.notional)
            greeks = black_scholes_greeks(S, K, T, r_d, sigma, r_f, args.type)
            result = {
                "price": round(price, 4),
                "price_per_unit": round(price / args.notional, 6) if args.notional > 0 else 0,
                "notional": args.notional,
                "greeks": greeks,
            }
        elif args.command == "swap_value":
            result = swap_value(
                args.effective_date, args.maturity_date,
                args.fixed_rate, args.freq, args.notional, args.discount_rate
            )
        elif args.command == "cds_value":
            result = cds_value(
                args.valuation_date, args.maturity_date,
                args.recovery_rate, args.notional, args.spread_bps,
                args.discount_rate
            )
        elif args.command == "forward_price":
            result = forward_price(
                args.spot, args.rate / 100, args.time, args.div_yield / 100
            )
        else:
            result = {"error": f"Unknown command: {args.command}"}

        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)


if __name__ == "__main__":
    main()
