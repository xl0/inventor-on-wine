#!/usr/bin/env python3
"""plot.py OUT: trend plots + text trends for a tools/soak/soak.sh run.

Writes OUT/steps.csv (iter, scenario, step, result, secs), OUT/probe.csv,
OUT/resources.png, OUT/timings.png, OUT/trends.txt. Trends are least-squares slopes
per hour of the per-iteration value (last sample of each iteration, so the
within-iteration sawtooth doesn't bias them), fitted per Inventor session (pid)."""
import csv, glob, os, re, sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OUT = sys.argv[1]
BLUE, ORANGE, INK, MUTED = '#2a78d6', '#eb6834', '#222222', '#8a8a85'
plt.rcParams.update({'font.size': 8, 'axes.edgecolor': MUTED, 'axes.labelcolor': INK, 'axes.grid': True,
                     'grid.color': '#e6e6e3', 'grid.linewidth': 0.6, 'lines.linewidth': 1.5,
                     'axes.spines.top': False, 'axes.spines.right': False})

mon = list(csv.DictReader(open(f'{OUT}/mon.csv')))
t0 = int(mon[0]['t'])
hrs = lambda t: (int(t) - t0) / 3600
num = lambda v: float(v) if v not in ('', None) else np.nan

# resprobe lines: "EPOCH h=N gdi=N win=N priv=NM ws=NM | ..."
probe = []
for f in sorted(glob.glob(f'{OUT}/probe-*.txt')):
    pid = re.search(r'probe-(\d+)', f).group(1)
    for l in open(f):
        m = re.match(r'(\d+) h=(\d+) gdi=(\d+) win=(\d+) priv=\d+M ws=(\d+)M', l)
        if m: probe.append(dict(t=m[1], inv=pid, h=m[2], gdi=m[3], win=m[4], ws=m[5]))
with open(f'{OUT}/probe.csv', 'w') as f:
    w = csv.DictWriter(f, ['t', 'inv', 'h', 'gdi', 'win', 'ws']); w.writeheader(); w.writerows(probe)

# attach iteration to probe rows from mon.csv (nearest earlier mon row)
mt = np.array([int(r['t']) for r in mon])
for r in probe:
    r['iter'] = mon[max(0, np.searchsorted(mt, int(r['t'])) - 1)]['iter']

# CPU% from tick deltas (100 Hz)
for key in ('inv_cpu', 'ws_cpu', 'xorg_cpu'):
    prev = None
    for r in mon:
        v, t = num(r[key]), int(r['t'])
        same = not key.startswith('inv') or (prev and prev[2] == r['inv'])
        r[key + '%'] = (v - prev[0]) / (t - prev[1]) if prev and same and v >= prev[0] and t > prev[1] else np.nan
        prev = (v, t, r['inv']) if not np.isnan(v) else None

# a pid change that persists (a transient second Inventor.exe is not a restart)
restarts = [hrs(b['t']) for a, b, c in zip(mon, mon[1:], mon[2:]) if a['inv'] and b['inv'] and a['inv'] != b['inv'] == c['inv']]
series = [  # (title, rows, key, scale)
    ('Inventor RSS (GB)', mon, 'inv_rss', 1 / 2**20), ('Inventor VSZ (GB)', mon, 'inv_vsz', 1 / 2**20),
    ('Inventor threads', mon, 'inv_thr', 1), ('Inventor open fds', mon, 'inv_fds', 1),
    ('Inventor mappings', mon, 'inv_maps', 1), ('Inventor CPU (%)', mon, 'inv_cpu%', 1),
    ('Windows kernel handles', probe, 'h', 1), ('GDI objects', probe, 'gdi', 1),
    ('USER windows', probe, 'win', 1), ('wineserver RSS (MB)', mon, 'ws_rss', 1 / 1024),
    ('wineserver CPU (%)', mon, 'ws_cpu%', 1), ('wineserver fds', mon, 'ws_fds', 1),
    ('Xorg :98 RSS (MB)', mon, 'xorg_rss', 1 / 1024), ('Xorg :98 CPU (%)', mon, 'xorg_cpu%', 1),
    ('Temp dir entries', mon, 'tmp_files', 1)]
fig, axs = plt.subplots(5, 3, figsize=(13, 13), sharex=True)
for ax, (title, rows, key, sc) in zip(axs.flat, series):
    x = [hrs(r['t']) for r in rows]; y = [num(r[key]) * sc for r in rows]
    ax.plot(x, y, color=BLUE, lw=1)
    for h in restarts: ax.axvline(h, color=ORANGE, lw=1, ls='--')
    ax.set_title(title, loc='left', color=INK)
for ax in axs[-1]: ax.set_xlabel('hours since start')
fig.suptitle('Soak: resources every 30 s (dashed = Inventor restart)', x=0.01, ha='left', color=INK)
fig.tight_layout(); fig.savefig(f'{OUT}/resources.png', dpi=110); plt.close(fig)

# per-step timings
steps = []
for d in sorted(glob.glob(f'{OUT}/iter[0-9]*/'), key=lambda d: int(d.rstrip('/')[len(OUT) + 5:])):
    it = int(d.rstrip('/')[len(OUT) + 5:])
    for f in glob.glob(f'{d}/*.txt'):
        s = os.path.basename(f)[:-4]
        if s in ('table', 'stderr'): continue
        for l in open(f, errors='replace'):
            m = re.match(r'(PASS|FAIL) (.*?) \(([0-9.]+)s\)', l)
            if m: steps.append(dict(iter=it, scenario=s, step=m[2], result=m[1], secs=float(m[3])))
with open(f'{OUT}/steps.csv', 'w') as f:
    w = csv.DictWriter(f, ['iter', 'scenario', 'step', 'result', 'secs']); w.writeheader(); w.writerows(steps)

iters = list(csv.DictReader(open(f'{OUT}/iters.csv')))
scen = sorted({s['scenario'] for s in steps if s['scenario'] != 'samples'})
n = len(scen) + 2
cols = 4; rows_ = (n + cols - 1) // cols
fig, axs = plt.subplots(rows_, cols, figsize=(13, 2.3 * rows_), sharex=True)
axl = list(axs.flat)
def iplot(ax, x, y, title):
    ax.plot(x, y, color=BLUE, marker='o', ms=3); ax.set_title(title, loc='left', color=INK); ax.set_ylim(bottom=0)
iplot(axl[0], [int(r['iter']) for r in iters], [num(r['suite_s']) for r in iters], 'suite wall time (s)')
sr = [r for r in iters if r['samples_s']]
iplot(axl[1], [int(r['iter']) for r in sr], [num(r['samples_s']) for r in sr], 'samples wall time (s; 1800 = run.sh timeout)')
for ax, s in zip(axl[2:], scen):
    tot = {}
    for x in steps:
        if x['scenario'] == s and x['step'] != 'connect': tot[x['iter']] = tot.get(x['iter'], 0) + x['secs']
    iplot(ax, sorted(tot), [tot[k] for k in sorted(tot)], f'{s}: step time excl. connect (s)')
for ax in axl[n:]: ax.axis('off')
for ax in axl[-cols:]: ax.set_xlabel('iteration')
fig.suptitle('Soak: timings per iteration', x=0.01, ha='left', color=INK)
fig.tight_layout(); fig.savefig(f'{OUT}/timings.png', dpi=110); plt.close(fig)

# trends
out = []
def slope(pts):
    if len(pts) < 3: return None
    x, y = np.array(pts).T
    return np.polyfit(x, y, 1)[0]
out.append('Resource trends: per-iteration last sample, slope per hour per Inventor session\n')
for title, rows, key, sc in series:
    if key.endswith('%'): continue
    by = {}
    for r in rows:
        v = num(r[key])
        if r.get('inv') and r.get('iter') not in (None, '', '0') and not np.isnan(v):
            by.setdefault(r['inv'], {})[r['iter']] = (hrs(r['t']), v * sc)
    parts = []
    for pid, d in by.items():
        pts = list(d.values()); s_ = slope(pts)
        if s_ is None: continue
        parts.append(f'pid {pid}: {pts[0][1]:.1f} -> {pts[-1][1]:.1f} over {pts[-1][0] - pts[0][0]:.1f} h, {s_:+.2f}/h')
    out.append(f'  {title:26s} ' + ('; '.join(parts) or 'n/a'))
out.append('\nCPU means (%): ' + ', '.join(f"{k}={np.nanmean([r[k] for r in mon]):.0f}" for k in ('inv_cpu%', 'ws_cpu%', 'xorg_cpu%')))
out.append('\nStep timing drift (steps >= 0.5 s mean; slope of secs vs iteration, top 15 by relative change over the run)')
by = {}
for x in steps:
    if x['step'] != 'connect': by.setdefault((x['scenario'], x['step']), []).append((x['iter'], x['secs']))
rows_ = []
for k, pts in by.items():
    y = np.array([p[1] for p in pts])
    if len(pts) < 4 or y.mean() < 0.5: continue
    s_ = slope(pts); span = max(p[0] for p in pts) - min(p[0] for p in pts)
    rows_.append((s_ * span / y.mean(), k, y[:3].mean(), y[-3:].mean(), s_))
for rel, k, a, b, s_ in sorted(rows_, key=lambda r: -abs(r[0]))[:15]:
    out.append(f'  {k[0]:10s} {k[1][:40]:40s} first3 {a:6.2f}s last3 {b:6.2f}s  {s_:+.3f}s/iter ({rel:+.0%})')
open(f'{OUT}/trends.txt', 'w').write('\n'.join(out) + '\n')
print('\n'.join(out))
