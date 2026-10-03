"""Walk-forward evaluation of every pre-print signal the ER Earnings tab uses
(and the candidates for its redesign). Every prediction for print i of a symbol
uses only that symbol's earlier prints; pooled constants are fitted on the
first half of calendar time and evaluated on the second.
"""
import json, os, sys, math
import numpy as np
import pandas as pd
from scipy import stats

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.DataFrame([json.loads(l) for l in open(os.path.join(HERE, "prints.jsonl"))])
df = df.sort_values(["symbol", "ts"]).reset_index(drop=True)
df["abn"] = df["reaction"] - df["spy_reaction"].fillna(0)
df["beat"] = np.where(df["suspect"] | df["surprise_pct"].isna(), np.nan,
                      (df["surprise_pct"] > 0).astype(float))
df["date"] = pd.to_datetime(df["ts"], unit="s")
print(f"prints={len(df)} symbols={df.symbol.nunique()} span={df.date.min().date()}..{df.date.max().date()}")
print(f"suspect surprises: {df.suspect.mean():.1%}")

# ── per-symbol walk-forward features ────────────────────────────────────────
K = 8  # the tab's track-record window
feats = []
for sym, g in df.groupby("symbol"):
    g = g.reset_index(drop=True)
    for i in range(len(g)):
        prior = g.iloc[max(0, i - K):i]
        prior_all = g.iloc[:i]
        cur = g.iloc[i]
        f = {"idx": cur.name, "symbol": sym, "date": cur.date, "n_prior": len(prior_all)}
        f["reaction"] = cur.reaction; f["abn"] = cur.abn; f["beat"] = cur.beat
        f["surprise"] = cur.surprise_pct if not cur.suspect else np.nan
        f["runup5"] = cur.runup5; f["runup20"] = cur.runup20; f["pre_vol"] = cur.pre_vol
        if len(prior) >= 4:
            f["trail_abs"] = prior.reaction.abs().mean()
            f["trail_abs_all"] = prior_all.reaction.abs().mean()
            f["trail_abs_med"] = prior.reaction.abs().median()
            f["trail_abn_abs"] = prior.abn.abs().mean()
            f["drift"] = prior.reaction.mean()
            f["drift_abn"] = prior.abn.mean()
            s = prior.surprise_pct[~prior.suspect]
            f["beat_bias"] = s.mean() if len(s) else np.nan
            f["beat_ir"] = s.mean() / s.std(ddof=0) if len(s) >= 3 and s.std(ddof=0) > 0 else np.nan
            b = prior.beat.dropna()
            f["beats_prior"] = b.sum(); f["n_beat_prior"] = len(b)
            br = prior.reaction[(prior.beat == 1)]
            f["beat_paid"] = br.mean() if len(br) >= 3 else np.nan
            mr = prior.reaction[(prior.beat == 0)]
            f["miss_paid"] = mr.mean() if len(mr) >= 2 else np.nan
            # asked growth: consensus vs last actual
            prev_act = g.iloc[i - 1].eps_actual
            f["ask"] = ((cur.eps_estimate - prev_act) / abs(prev_act) * 100
                        if cur.eps_estimate is not None and prev_act and abs(prev_act) > 0.005 else np.nan)
            # beat-size history (positive surprise only) - for reaction sensitivity
            ss = prior[~prior.suspect & prior.surprise_pct.notna()]
            if len(ss) >= 4 and ss.surprise_pct.std() > 0:
                f["sens"] = np.polyfit(ss.surprise_pct.clip(-30, 30), ss.reaction, 1)[0]
        feats.append(f)
F = pd.DataFrame(feats)
F = F[F.n_prior >= 4].copy()
print(f"walk-forward evaluable prints: {len(F)}")
split = F.date.quantile(0.5)
print(f"time split at {split.date()}")

def ic(x, y):
    m = x.notna() & y.notna()
    if m.sum() < 30: return (np.nan, np.nan, int(m.sum()))
    r, p = stats.spearmanr(x[m], y[m])
    return (r, p, int(m.sum()))

def hit(x, y):
    m = x.notna() & y.notna() & (x.abs() > 1e-9)
    return ((np.sign(x[m]) == np.sign(y[m])).mean(), int(m.sum()))

print("\n══ 1. SIZE — forecasting |move| ══")
F["absr"] = F.reaction.abs()
F["blend"] = 0.5 * F.trail_abs + 1.0 * F.pre_vol
cands = {
    "trailing mean |r| (8q)": F.trail_abs,
    "trailing mean |r| (all prior)": F.trail_abs_all,
    "trailing median |r| (8q)": F.trail_abs_med,
    "tab blend 0.5*trail+1.0*vol": F.blend,
    "vol only x2.0": 2.0 * F.pre_vol,
}
test = F.date >= split
# fit OLS blend on train half
tr = F[(~test) & F.trail_abs.notna() & F.pre_vol.notna() & F.trail_abs_all.notna()]
X = np.c_[tr.trail_abs_all, tr.pre_vol]
coef, *_ = np.linalg.lstsq(X, tr.absr, rcond=None)
print(f"OLS (no intercept) on train: |r| = {coef[0]:.2f}*trail_all + {coef[1]:.2f}*vol")
cands["OLS blend (train-fit)"] = coef[0] * F.trail_abs_all + coef[1] * F.pre_vol
for name, pred in cands.items():
    m = test & pred.notna() & F.pre_vol.notna() & F.trail_abs.notna()
    e = (pred[m] - F.absr[m]).abs().mean()
    c = np.corrcoef(pred[m], F.absr[m])[0, 1]
    bias = (pred[m] - F.absr[m]).mean()
    print(f"  {name:34s} MAE {e:.3f}  corr {c:+.3f}  bias {bias:+.2f}  n={m.sum()}")

# ratio distribution |r| / blend → for honest ranges
m = F.blend.notna() & F.absr.notna()
ratio = (F.absr[m] / F.blend[m])
print("  |r| / blend quantiles:", {q: round(ratio.quantile(q), 2) for q in (0.25, 0.5, 0.68, 0.8, 0.9, 0.95)})
print(f"  P(|r| <= blend) = {(ratio <= 1).mean():.2f}")
mt = F.trail_abs.notna() & F.absr.notna()
rt = F.absr[mt] / F.trail_abs[mt]
print("  |r| / trailing quantiles:", {q: round(rt.quantile(q), 2) for q in (0.25, 0.5, 0.68, 0.8, 0.9, 0.95)})

print("\n══ 2. BEAT PROBABILITY ══")
B = F[F.beat.notna() & F.n_beat_prior.notna() & (F.n_beat_prior >= 4)].copy()
base_train = F[(~test) & F.beat.notna()].beat.mean()
print(f"  pooled beat base rate (train half) {base_train:.3f}; test half {F[test & F.beat.notna()].beat.mean():.3f}")
for k in (0, 2, 4, 8, 16):
    p = (B.beats_prior + k * base_train) / (B.n_beat_prior + k) if k else B.beats_prior / B.n_beat_prior
    p = p.clip(0.02, 0.98)
    tb = B.date >= split
    brier = ((p[tb] - B.beat[tb]) ** 2).mean()
    print(f"  shrink k={k:2d}: Brier {brier:.4f}")
brier0 = ((base_train - B.beat[B.date >= split]) ** 2).mean()
print(f"  base rate only : Brier {brier0:.4f}")
B["p_beat"] = ((B.beats_prior + 4 * base_train) / (B.n_beat_prior + 4))
B["pbin"] = pd.cut(B.p_beat, [0, .5, .65, .75, .85, .95, 1])
print(B.groupby("pbin", observed=True).agg(pred=("p_beat", "mean"), realised=("beat", "mean"), n=("beat", "size")).round(3).to_string())
r, p_, n = ic(B.ask, B.beat); print(f"  asked growth vs beat: IC {r:+.3f} p={p_:.3g} n={n}")
r, p_, n = ic(B.beat_bias, F.surprise.reindex(B.index)); print(f"  prior beat bias vs next surprise: IC {r:+.3f} p={p_:.3g} n={n}")

print("\n══ 3. DIRECTION — each backward leg vs next move (pooled, walk-forward) ══")
legs = {
    "drift (prior mean reaction)": F.drift,
    "drift abnormal": F.drift_abn,
    "beat bias (avg surprise)": F.beat_bias,
    "beat IR (avg/stdev)": F.beat_ir,
    "beats get paid": F.beat_paid,
    "run-up 5d": F.runup5,
    "run-up 20d": F.runup20,
    "-run-up 20d (crowding)": -F.runup20,
    "pre vol": F.pre_vol,
    "asked growth": F.ask,
}
for target in ("reaction", "abn"):
    print(f"  target = {target}")
    for name, x in legs.items():
        r, p_, n = ic(x, F[target])
        r2, p2, n2 = ic(x[test], F[target][test])
        print(f"    {name:30s} IC {r:+.3f} (p={p_:.2g}, n={n})   test-half IC {r2:+.3f} (p={p2:.2g})")
print(f"  pooled mean reaction {F.reaction.mean():+.3f}  abn {F.abn.mean():+.3f}  up-rate {(F.reaction>0).mean():.3f}")

print("\n══ 4. SCENARIOS — reaction given the outcome ══")
G = F[F.beat.notna()].copy()
G["sbin"] = pd.cut(G.surprise, [-1e9, -10, -2, 0, 2, 5, 10, 1e9])
print(G.groupby("sbin", observed=True).agg(mean=("reaction", "mean"), median=("reaction", "median"),
      up=("reaction", lambda s: (s > 0).mean()), mean_abn=("abn", "mean"), n=("reaction", "size")).round(2).to_string())
print(f"  mean reaction | beat {G[G.beat==1].reaction.mean():+.2f}  | miss {G[G.beat==0].reaction.mean():+.2f}")
print(f"  up-rate       | beat {(G[G.beat==1].reaction>0).mean():.2f}  | miss {(G[G.beat==0].reaction>0).mean():.2f}")
# does a name's own past "if beat" reaction predict its next beat reaction?
GB = G[G.beat == 1]
r, p_, n = ic(GB.beat_paid, GB.reaction); print(f"  persistence of 'if beat' reaction per name: IC {r:+.3f} p={p_:.2g} n={n}")
GM = G[G.beat == 0]
r, p_, n = ic(GM.miss_paid, GM.reaction); print(f"  persistence of 'if miss' reaction per name: IC {r:+.3f} p={p_:.2g} n={n}")
r, p_, n = ic(G.surprise, G.reaction); print(f"  surprise vs reaction (contemporaneous): IC {r:+.3f} n={n}")
# sensitivity persistence: does per-name slope predict
G["sens_pred"] = G.sens * G.surprise.clip(-30, 30)
r, p_, n = ic(G.sens_pred, G.reaction); print(f"  per-name sensitivity x actual surprise: IC {r:+.3f} n={n}")
# within-name standardized: given beat, |reaction| tail asymmetry
print(f"  given beat: P(down) {(GB.reaction<0).mean():.2f};  given beat & runup20 top tercile: P(down) "
      f"{(GB[GB.runup20 > GB.runup20.quantile(2/3)].reaction<0).mean():.2f}")

print("\n══ 5. Scenario EV as a direction call ══")
pb = ((F.beats_prior + 4 * base_train) / (F.n_beat_prior + 4))
ev = pb * F.beat_paid.fillna(G[G.beat==1].reaction.mean()) + (1 - pb) * F.miss_paid.fillna(G[G.beat==0].reaction.mean())
r, p_, n = ic(ev, F.reaction); h = hit(ev, F.reaction)
print(f"  scenario EV: IC {r:+.3f} p={p_:.2g} n={n}  hit {h[0]:.3f} (n={h[1]})")
for name in ("drift (prior mean reaction)", "beats get paid", "-run-up 20d (crowding)"):
    h = hit(legs[name], F.reaction); print(f"  hit-rate {name:30s} {h[0]:.3f} n={h[1]}")
print(f"  always-up hit rate {(F.reaction>0).mean():.3f}")

F.to_pickle(os.path.join(HERE, "features.pkl"))

print("\n══ 6. The tab's SCORECARD reconstruction (backward legs, same constants) ══")
def clamp(v): return max(-1.0, min(1.0, v))
def blend(parts):
    s = w = 0
    for v, wt in parts:
        if v is None or (isinstance(v, float) and math.isnan(v)): continue
        s += v * wt; w += wt
    return s / w if w else None
rows = []
for sym, g in df.groupby("symbol"):
    g = g.reset_index(drop=True)
    for i in range(4, len(g)):
        prior = g.iloc[max(0, i - 8):i].iloc[::-1]  # newest first
        cur = g.iloc[i]
        # track record
        s = prior[~prior.suspect & prior.surprise_pct.notna()]
        tr = None
        if len(s):
            avg = s.surprise_pct.mean(); sd = s.surprise_pct.std(ddof=0)
            cons = clamp((avg / sd) / 1.5) if len(s) >= 3 and sd > 1e-9 else clamp(avg / 10)
            br = s.reaction[s.surprise_pct > 0]
            paid = clamp(br.mean() / 5) if len(br) >= 3 else None
            tr = clamp(blend([(cons, .6), (paid, .4)]))
        # reaction leg (with hot-runup conditional)
        rc = None
        up = (prior.reaction > 0).mean(); avgr = prior.reaction.mean()
        rus = prior.runup5.dropna()
        cond = None
        if len(rus) >= 6 and not math.isnan(cur.runup5 if cur.runup5 is not None else float('nan')):
            med = sorted(rus)[len(rus) // 2]
            if cur.runup5 >= med:
                hot = prior[prior.runup5 >= med].reaction
                if len(hot) >= 3: cond = hot.mean()
        rc = clamp(cond / 5) if cond is not None else clamp(0.5 * (2 * up - 1) + 0.5 * clamp(avgr / 5))
        # crowding (absolute 20d only, as in the reconstruction)
        cr = clamp(-cur.runup20 / 15) if cur.runup20 is not None and not math.isnan(cur.runup20) else None
        parts = [(tr, .10), (rc, .09), (cr, .09)]
        wsum = sum(w for v, w in parts if v is not None)
        if not wsum: continue
        score = sum(v * w for v, w in parts if v is not None) / wsum * 100
        rows.append({"symbol": sym, "date": cur.date, "score": score, "tr": tr, "rc": rc, "cr": cr,
                     "reaction": cur.reaction, "abn": cur.abn})
S = pd.DataFrame(rows)
for col in ("score", "tr", "rc", "cr"):
    r, p_, n = ic(S[col], S.reaction); h = hit(S[col], S.reaction)
    print(f"  {col:6s} IC {r:+.3f} p={p_:.2g} n={n}  hit {h[0]:.3f}")
S["verdict"] = np.where(S.score >= 20, "BUY", np.where(S.score <= -20, "SELL", "HOLD"))
print(S.groupby("verdict").agg(mean=("reaction", "mean"), up=("reaction", lambda s: (s > 0).mean()), n=("reaction", "size")).round(3).to_string())
