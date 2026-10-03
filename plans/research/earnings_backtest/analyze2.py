import pandas as pd, numpy as np, warnings; warnings.filterwarnings("ignore")
from scipy import stats
F = pd.read_pickle("features.pkl")
F["size"] = (0.5*F.trail_abs + 1.0*F.pre_vol).fillna(F.trail_abs)
F["z"] = F.reaction / F["size"]
G = F[F.surprise.notna()].copy()
edges = [-1e9, 0, 3, 10, 1e9]; labels = ["miss", "beat 0-3%", "beat 3-10%", "beat >10%"]
G["bin"] = pd.cut(G.surprise, edges, labels=labels, right=True)
split = F.date.quantile(0.5)
for nm, part in (("ALL", G), ("train", G[G.date < split]), ("test", G[G.date >= split])):
    t = part.groupby("bin", observed=True).agg(share=("z","size"), z_mean=("z","mean"), z_med=("z","median"),
        up=("reaction", lambda s:(s>0).mean()), r_mean=("reaction","mean"), absz=("z", lambda s: s.abs().mean()))
    t["share"] /= t.share.sum(); print(nm); print(t.round(3).to_string())
# walk-forward per-name bin probabilities, shrunk to pooled (train) shares, k=8
tr = G[G.date < split]
pooled = tr.bin.value_counts(normalize=True).reindex(labels)
upb = tr.groupby("bin", observed=True).reaction.apply(lambda s:(s>0).mean()).reindex(labels)
zb = tr.groupby("bin", observed=True).z.mean().reindex(labels)
rows=[]
for sym, g in G.sort_values("date").groupby("symbol"):
    g=g.reset_index(drop=True)
    for i in range(4,len(g)):
        prior=g.iloc[max(0,i-8):i]
        cnt=prior.bin.value_counts().reindex(labels).fillna(0)
        p=(cnt+8*pooled)/(len(prior)+8)
        rows.append(dict(date=g.date[i], pup=(p*upb).sum(), ez=(p*zb).sum(), pbeat=1-p["miss"],
            up=float(g.reaction[i]>0), beat=float(g.surprise[i]>0), r=g.reaction[i], size=g["size"][i]))
W=pd.DataFrame(rows); T=W[W.date>=split]
print("\nscenario P(up) test: Brier", round(((T.pup-T.up)**2).mean(),4), " vs 0.5:", round(((0.5-T.up)**2).mean(),4),
      " vs train up-rate:", round(((tr.reaction.gt(0).mean()-T.up)**2).mean(),4))
print("P(up) range", T.pup.describe()[["min","25%","50%","75%","max"]].round(3).to_dict())
print("IC ez vs r", round(stats.spearmanr(T.ez, T.r)[0],3), "p", round(stats.spearmanr(T.ez,T.r)[1],3))
print("P(beat) Brier", round(((T.pbeat-T.beat)**2).mean(),4), "vs base", round(((tr.surprise.gt(0).mean()-T.beat)**2).mean(),4))
# interval coverage in units of size
z=F.z.dropna().abs()
print("\n|z| quantiles", {q: round(z.quantile(q),2) for q in (.5,.68,.8,.9,.95)})
# up rate by crowding tercile
F["ru_t"]=pd.qcut(F.runup20,3,labels=["low","mid","high"])
print(F.groupby("ru_t",observed=True).agg(mean=("reaction","mean"),up=("reaction",lambda s:(s>0).mean()),n=("reaction","size")).round(3).to_string())
print("runup20 tercile cutoffs:", F.runup20.quantile([1/3, 2/3]).round(2).to_dict())
# P(beat) calibration with k=8 on test half
print("\nP(beat) k=8 calibration, test half:")
T["pb"] = pd.cut(T.pbeat, [0, .6, .7, .8, .9, 1])
print(T.groupby("pb", observed=True).agg(pred=("pbeat","mean"), realised=("beat","mean"), n=("beat","size")).round(3).to_string())
print("pooled beat rate ALL:", round(G.surprise.gt(0).mean(), 3), " n prints", len(F), " symbols", F.symbol.nunique())
