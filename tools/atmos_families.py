"""Rhythm families of the Izzamuzzic passport (docs/audit/atmos/ATMOS_PASSPORT.md).

usage: python tools/atmos_families.py <atmos_passport out folder>

Families come from each track's two most common kick / snare bars (the report table), not from a
threshold; the lists below are exact track-name prefixes. Maintainer's ear (2026-10-14): B is not rap,
the fast ones are drum & bass (tempo as measured); D1 is a style of its own ("a cool one").
"""
import json, os, sys, statistics as st, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atmos_passport as ap
OUT = sys.argv[1] if len(sys.argv) > 1 else 'out'
fam = {
 'A': ['05. instinct','07. night','04. meela','03. behind','02. see','04 - Samsara','06 - Time','07 - Y.W.Y.H.','08 - Not So Far','08 - Mini Mal','13 - Sai','09 - Sxx','10 - Missed','09 - Black Magic','02 - W.a.y.n.','02 - You will','13 - Escape','01 - Ash','04 - Run from','05 - Blow','03 - Fly','03 - Feala','05 - Catch Me','04 - Say','08 - Cherry','10 - Breathe In','11 - Tonight'],
 'B': ['06 - Ayrton','06 - Crystal','16 - I Feel','10 - Stop','07 - Champion','07 - Speed Up','09. outside','03 - Want Take','05 - Ghost'],
 'C': ['18 - Exit','03 - Shutdown','15 - Tonight (Dubstep','16 - The Drum','01 - Wakez'],
 'D1': ['01 - Run','01 - Sun','01. azi','12 - Mad','14 - Autumn','18 - Вселенная'],
}
TITLES = {'A': 'straight kick + clap on 2 / 4, sidechain bass', 'B': 'two-step / drum & bass bar (kick 1 + 11, snare 5 + 13)',
          'C': 'dubstep / half-time', 'D1': 'broken (syncopated, through-composed)', 'D2': 'sparse / unclear sections'}
def which(n):
    for k, keys in fam.items():
        if any(n.startswith(key) for key in keys): return k
    return 'D2'
groups = collections.defaultdict(list)
for f in sorted(os.listdir(OUT)):
    if not f.endswith('.json'): continue
    r = ap.track_stats(json.load(open(os.path.join(OUT, f), encoding='utf-8')))
    if r.get('bars'): groups[which(r['name'])].append(r)
for g, rs in sorted(groups.items()):
    b = [r for r in rs if 'bass_notes_bar' in r]
    bars = sum(r['bars'] for r in rs)
    print(f"## {g} {TITLES[g]}: {len(rs)} tracks, {bars} bars, bpm {sorted(round(r['bpm']) for r in rs)}")
    print('   ', ', '.join(r['name'][:22] for r in rs))
    for short in 'ksh':
        prof = [sum(r[short+'_prof'][i]*r['bars'] for r in rs)/bars for i in range(16)]
        print(f"    {short} " + ' '.join(f'{x:.2f}' for x in prof))
    m = lambda k: st.median(r[k] for r in b)
    print(f"    K/bar {st.median(r['k_per_bar'] for r in rs):.1f} S/bar {st.median(r['s_per_bar'] for r in rs):.1f} H/bar {st.median(r['h_per_bar'] for r in rs):.1f} | K bar = previous {st.median(r['k_rep1'] for r in rs):.2f} | hats on 8ths {st.median(r['hat_8th_share'] for r in rs):.2f} | swing {st.median(r['swing'] for r in rs):.0f}")
    print(f"    kicks off the beat {st.median(r['kick_offbeat'] for r in rs):.2f} | snares off 2 / 4 {st.median(r['snare_off_backbeat'] for r in rs):.2f} | bars heard once {st.median(r['unique_bars'] for r in rs):.2f} | tempo conf {st.median(r['conf'] for r in rs):.2f}")
    print(f"    bass notes/bar {m('bass_notes_bar'):.1f}, pitch changes/bar {m('bass_changes_bar'):.1f}, median length {m('bass_dur_beats'):.2f} beats, >=2 beats {m('bass_long_share'):.2f}, register midi {m('bass_register'):.0f}")
    dk = [r['duck_db'] for r in b if 'duck_db' in r]
    print(f"    ducking median {st.median(dk):.0f} dB, >=10 dB in {sum(1 for x in dk if x>=10)}/{len(dk)}; recover {st.median(r['duck_ms'] for r in b if 'duck_ms' in r):.0f} ms")
    iv = collections.Counter(); rel = collections.Counter()
    for r in b:
        for k,v in r['bass_intervals'].items(): iv[k]+=v/len(b)
        for k,v in r['bass_rel'].items(): rel[k]+=v/len(b)
    print('    intervals', ', '.join(f'{k} {v:.2f}' for k,v in iv.most_common()))
    print('    bass vs kick', ', '.join(f'{k} {v:.2f}' for k,v in rel.most_common()))
