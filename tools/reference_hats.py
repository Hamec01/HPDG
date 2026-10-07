"""Hat probability per 16th where the bar has no kick / snare (roadmap Phase 2, RULES 18 / 19 / 30).

usage: python tools/reference_hats.py <reports folder> [<lab patterns.csv> ...]

A transcribed hat under a kick or snare often falls below confidence 0.5 (masking), so the raw
hat profile of played loops under-counts beats 1-4. Here each position is only counted in bars
where no kick / snare sits on it (HPDG: snare taken as 4 and 12). "all free offbeats" = share of
bars that play every off-beat 8th (2 / 6 / 10 / 14) not covered by a kick / snare.
Same calibration / validation split as tools/reference_kicks.py.
"""
import collections, csv, hashlib, os, re, sys
folder, gen = sys.argv[1], sys.argv[2:]
def split_of(n): return 'validation' if int(hashlib.md5(n.encode('utf-8')).hexdigest(),16)%10<3 else 'calibration'
ref={'calibration':[], 'validation':[]}
for name in sorted(os.listdir(folder)):
    if not name.endswith('.txt'): continue
    t=open(os.path.join(folder,name),encoding='utf-8',errors='replace').read()
    m=re.search(r'\| bars (\d+) \|',t); nb=int(m.group(1)) if m else 0
    lanes={L:collections.defaultdict(set) for L in ('Kick','Snare','HiHat')}
    for h in re.finditer(r'^(Kick|Snare|HiHat)\s+t=\S+ bar (\d+) 16th \S+ grid (\d+) off \S+ vel \d+ conf ([0-9.]+)',t,re.M):
        if float(h.group(4))>=0.5: lanes[h.group(1)][int(h.group(2))].add((int(h.group(3))%3840)//240)
    for b in range(1,nb+1):
        ref[split_of(name)].append((lanes['Kick'][b]|lanes['Snare'][b], lanes['HiHat'][b]))
def show(title,bars):
    out=[]
    for p in range(16):
        free=[h for occ,h in bars if p not in occ]
        out.append(sum(p in h for h in free)/max(1,len(free)))
    full=sum(1 for occ,h in bars if all(p in h for p in (2,6,10,14) if p not in occ))/max(1,len(bars))
    print(f'{title:38s} '+' '.join(f'{x:4.2f}' for x in out)+f' | all free offbeats {full:4.2f}')
print(' '*39+' '.join(f'{p:4d}' for p in range(16)))
for s in ref: show('REF '+s, ref[s])
for path in gen:
    by=collections.defaultdict(list)
    for r in csv.DictReader(open(path,encoding='utf-8')):
        kb=r['kickBars'].split('|'); hb=r['hatBars'].split('|')
        for i in range(int(r['bars'])):
            k={int(x) for x in kb[i].split()}|{4,12}; h={int(x) for x in hb[i].split()}
            by[(r['substyle'],r['density'],r['bars'])].append((k,h))
    for key,b in sorted(by.items()): show('HPDG '+' '.join(key), b)
