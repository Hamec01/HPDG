"""Kick statistics of full tracks (no stems) transcribed with HPDG_BreakLab (roadmap Phase 2).

usage: python tools/track_kicks.py <reports folder> [min tempo confidence, default 0.6]

<reports folder>: `HPDG_BreakLab analyze <track> --hits` reports, one .txt per track, file name
`<artist>_<nn>.txt`. A full mix is unreliable material (vocals read as snares, bass masks kicks;
RULE 34), so a track is used only when its tempo confidence >= min, 70-100 BPM, a kick on beat 1 in
>= 60 % of its bars and >= 6 bars with kicks; bars without any kick (intro / break) are skipped.
Prints per track, then kicks / bar, the kicks-per-bar distribution and the kick probability per
16th over the used bars, and kicks / bar per artist.
"""
import os,re,sys,collections
f=sys.argv[1]; minconf=float(sys.argv[2]) if len(sys.argv)>2 else 0.6
keep=[]; allbars=[]; byart=collections.defaultdict(list)
for n in sorted(os.listdir(f)):
    t=open(os.path.join(f,n),encoding='utf-8',errors='replace').read()
    m=re.search(r'bpm +([0-9.]+) bars (\d+) conf ([0-9.]+)',t); bpm,nb,conf=float(m.group(1)),int(m.group(2)),float(m.group(3))
    pb=collections.defaultdict(set)
    for h in re.finditer(r'^Kick\s+t=\S+ bar (\d+) 16th \S+ grid (\d+) off \S+ vel \d+ conf ([0-9.]+)',t,re.M):
        if float(h.group(3))>=0.5: pb[int(h.group(1))].add((int(h.group(2))%3840)//240)
    bars=[pb[b] for b in range(1,nb+1) if pb[b]]          # skip bars without any kick (intro / break)
    d1=sum(0 in b for b in bars)/max(1,len(bars))
    ok=conf>=minconf and 70<=bpm<=100 and d1>=0.6 and len(bars)>=6
    kpb=sum(map(len,bars))/max(1,len(bars))
    print(f"{n[:-4]:10s} bpm {bpm:6.1f} conf {conf:.2f} bars {len(bars):2d} beat1 {d1:.2f} kicks/bar {kpb:.2f} {'USE' if ok else '-'}")
    if ok: allbars+=bars; byart[n.split('_')[0]].append(kpb)
print('\nused tracks', sum(map(len,byart.values())), 'bars', len(allbars))
c=collections.Counter(min(len(b),7) for b in allbars); T=len(allbars)
print('kicks/bar %.2f | dist '%(sum(map(len,allbars))/T)+' '.join(f'{k}:{100*c[k]/T:.0f}%' for k in range(1,8)))
p=[sum(s in b for b in allbars)/T for s in range(16)]
print('16th: '+' '.join(f'{s:4d}' for s in range(16))); print('p   : '+' '.join(f'{x:4.2f}' for x in p))
for a,v in byart.items(): print(a, len(v), 'tracks, kicks/bar', ' '.join(f'{x:.2f}' for x in v))
