"""Research passport of a reference artist's tracks (drums + bass), for a possible Atmospheric engine.

usage:
  python tools/atmos_passport.py run <tracks.txt> <stems root> <out folder> [--jobs N]
  python tools/atmos_passport.py report <out folder> [--md <file>]

<tracks.txt>: one mp3 per line. <stems root>: Demucs output (`python -m demucs -n htdemucs --flac -o <x>`),
i.e. <stems root>/<track name>/drums.flac + bass.flac.

Per track:
  - tempo from the whole drums stem (HPDG_BreakLab analyze: first 64 s + a second window, as the plugin);
  - two 40 s windows from 30 % and 60 % of the song (past the intro), drums transcribed at that tempo
    (`analyze --bpm T --cut --hits`), bass transcribed (`lines --print`) on the same windows;
  - the bass stem's low band (< 250 Hz) around each kick: how deep and how long the bass ducks
    (sidechain), because a ducked bass note "starts" 50-150 ms after the kick it sits on.

Hits with confidence >= 0.5 only (Sample Analysis step 11: >= 0.5 is right 71-96 %). Audio stays
out of git; the out folder keeps raw reports, the report prints numbers only.
"""
import collections
import concurrent.futures as cf
import json
import os
import re
import statistics as st
import subprocess
import sys

LAB = os.path.join(os.path.dirname(__file__), '..', 'build', 'Release', 'HPDG_BreakLab.exe')
WINDOWS = (0.30, 0.60)
WINDOW_SECONDS = 40.0
MIN_CONF = 0.5
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def lab(*args):
    return subprocess.run([LAB, *args], capture_output=True, text=True, encoding='utf-8', errors='replace',
                          stdin=subprocess.DEVNULL).stdout


def duration(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path],
                         capture_output=True, text=True, stdin=subprocess.DEVNULL).stdout
    return float(out.strip() or 0)


def cut(src, dst, start):
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', f'{start:.3f}', '-t', f'{WINDOW_SECONDS}', '-i', src,
                    '-ac', '1', dst], check=True, stdin=subprocess.DEVNULL)  # ffmpeg reads stdin otherwise


def stem(folder, name):
    for ext in ('.flac', '.wav'):
        p = os.path.join(folder, name + ext)
        if os.path.exists(p):
            return p
    return None


def parse_hits(text):
    hits = []
    for m in re.finditer(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+) bar (\d+) 16th ([0-9.]+) grid (-?\d+) off (-?\d+) vel (\d+) conf ([0-9.]+)',
                         text, re.M):
        hits.append(dict(lane=m.group(1), t=float(m.group(2)), bar=int(m.group(3)), grid=int(m.group(5)),
                         off=int(m.group(6)), vel=int(m.group(7)), conf=float(m.group(8))))
    head = re.search(r'Drum break: bpm ([0-9.]+) \| bars (\d+) \| tempo conf ([0-9.]+) \| swing ([0-9.]+)% \| origin (-?[0-9.]+) ms', text)
    info = dict(bpm=float(head.group(1)), bars=int(head.group(2)), conf=float(head.group(3)),
                swing=float(head.group(4)), origin=float(head.group(5)) / 1000.0) if head else None
    return info, hits


def parse_bass(text):
    part = text.split('melody line')[0]
    notes = []
    for m in re.finditer(r' ([A-G]#?)(-?\d)@([0-9.]+)\+([0-9.]+)', part):
        midi = NAMES.index(m.group(1)) + 12 * (int(m.group(2)) + 1)
        notes.append(dict(midi=midi, start=float(m.group(3)), dur=float(m.group(4))))
    key = re.search(r'\| key ([A-G]#? \w+)', text)
    return notes, key.group(1) if key else ''


def ducking(bass_path, kicks):
    """Mean low-band level of the bass stem around kicks: dB at kick+40 ms vs kick-60 ms, and the ms until it is back within 3 dB."""
    import numpy as np
    import scipy.signal as ss
    import soundfile as sf
    y, sr = sf.read(bass_path, always_2d=True)
    y = y.mean(axis=1)
    b, a = ss.butter(4, 250 / (sr / 2))
    yl = ss.filtfilt(b, a, y)
    hop = int(sr * 0.005)
    env = np.sqrt(np.convolve(yl ** 2, np.ones(hop * 2) / (hop * 2), 'same'))[::hop]
    db = 20 * np.log10(env + 1e-7)
    rows = []
    for k in kicks:
        i = int(k / 0.005)
        if i - 12 < 0 or i + 60 >= len(db):
            continue
        rows.append(db[i - 12:i + 60])  # -60 .. +300 ms
    if len(rows) < 4:
        return None
    prof = np.median(np.array(rows), axis=0)
    before = prof[0:4].mean()  # -60..-40 ms
    if before < -60:  # the bass is mostly silent before kicks: nothing to duck
        return dict(depth=0.0, recover_ms=0.0, level_before=float(before))
    low = prof[12:24]  # 0..60 ms
    dip = float(before - low.min())
    rec = 0.0
    lowest = 12 + int(np.argmin(low))
    for j in range(lowest, len(prof)):
        if prof[j] >= before - 3:
            rec = (j - 12) * 5.0
            break
    else:
        rec = 300.0
    return dict(depth=dip, recover_ms=rec, level_before=float(before))


def one(track, stems_root, out):
    name = os.path.splitext(os.path.basename(track))[0]
    folder = os.path.join(stems_root, name)
    drums, bass = stem(folder, 'drums'), stem(folder, 'bass')
    if not drums or not bass:
        return name, 'no stems'
    result_path = os.path.join(out, name + '.json')
    if os.path.exists(result_path):
        return name, 'cached'
    tmp = os.path.join(out, 'tmp')
    os.makedirs(tmp, exist_ok=True)
    whole = lab('analyze', drums)
    m = re.search(r' bpm +([0-9.]+) bars \d+ conf ([0-9.]+) swing ([0-9.]+) \| K (\d+) S (\d+) H (\d+) \| loop ([0-9.]+)', whole)
    if not m:
        return name, 'tempo failed'
    bpm, tconf = float(m.group(1)), float(m.group(2))
    dur = duration(drums)
    windows = []
    for w, frac in enumerate(WINDOWS):
        start = max(0.0, min(dur - WINDOW_SECONDS, dur * frac))
        wd, wb = os.path.join(tmp, f'{name}_{w}_d.wav'), os.path.join(tmp, f'{name}_{w}_b.wav')
        cut(drums, wd, start)
        cut(bass, wb, start)
        drum_text = lab('analyze', wd, '--bpm', f'{bpm}', '--cut', '--hits')
        bass_text = lab('lines', wb, '--print')
        open(os.path.join(out, f'{name}_{w}_drums.txt'), 'w', encoding='utf-8').write(drum_text)
        open(os.path.join(out, f'{name}_{w}_bass.txt'), 'w', encoding='utf-8').write(bass_text)
        info, hits = parse_hits(drum_text)
        notes, key = parse_bass(bass_text)
        kicks = [h['t'] for h in hits if h['lane'] == 'Kick' and h['conf'] >= MIN_CONF]
        windows.append(dict(start=start, info=info, hits=hits, bass=notes, key=key, duck=ducking(wb, kicks)))
        os.remove(wd)
        os.remove(wb)
    json.dump(dict(name=name, track=track, bpm=bpm, tempo_conf=tconf, duration=dur, windows=windows),
              open(result_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    return name, f'bpm {bpm} conf {tconf}'


# ---------------------------------------------------------------------------------------------
def phase_shift(win):
    """16ths to subtract (0-3) so that confident kicks sit on the beat and snares on 2 / 4. A straight
    kick read with beat 1 an 8th / 16th off puts most kicks on one off-beat slot of every beat. Score of
    a shift: kicks on a beat + 2 x snares on 16ths 5 / 13; shift only with >= 8 kicks and a score
    >= 1.5 x the unshifted one (a busy kick with a clear 2 / 4 snare, as in dubstep, stays)."""
    kicks = [h['grid'] // 240 for h in win['hits'] if h['lane'] == 'Kick' and h['conf'] >= MIN_CONF]
    snares = [h['grid'] // 240 for h in win['hits'] if h['lane'] == 'Snare' and h['conf'] >= MIN_CONF]
    if len(kicks) < 8:
        return 0
    def score(r):
        return sum(1 for k in kicks if (k - r) % 4 == 0) + 2 * sum(1 for s in snares if (s - r) % 16 in (4, 12))
    best = max(range(4), key=score)
    return best if best != 0 and score(best) >= 1.5 * max(1, score(0)) else 0


def bars_of(win, lane):
    """{bar: set of 16th slots} for hits of a lane with confidence >= MIN_CONF, after phase_shift."""
    shift = phase_shift(win)
    per = collections.defaultdict(set)
    for h in win['hits']:
        if h['lane'] == lane and h['conf'] >= MIN_CONF:
            slot = h['grid'] // 240 - shift
            per[slot // 16 + 1].add(slot % 16)
    return per


def track_stats(d):
    bpm = d['bpm']
    beat = 60.0 / bpm
    st16 = beat / 4
    out = dict(name=d['name'], bpm=bpm, conf=d['tempo_conf'])
    lanes = {'Kick': [], 'Snare': [], 'HiHat': []}
    nbars = 0
    hat_offgrid = []
    swing = []
    bass_rows = []
    ducks = []
    keys = []
    for win in d['windows']:
        info = win['info']
        if not info:
            continue
        # Bars inside the window that the drums play (any confident hit).
        per = {lane: bars_of(win, lane) for lane in lanes}
        played = sorted(set().union(*[set(p) for p in per.values()]))
        nbars += len(played)
        for lane in lanes:
            lanes[lane].extend(frozenset(per[lane].get(b, set())) for b in played)
        swing.append(info['swing'])
        for h in win['hits']:
            if h['lane'] == 'HiHat' and h['conf'] >= MIN_CONF:
                hat_offgrid.append(abs(h['off']))
        if win['duck']:
            ducks.append(win['duck'])
        keys.append(win['key'])
        # Bass vs kicks (seconds inside the window).
        kicks = sorted(h['t'] for h in win['hits'] if h['lane'] == 'Kick' and h['conf'] >= MIN_CONF)
        origin = info['origin']
        merged = []
        for n in win['bass']:
            if merged and merged[-1]['midi'] == n['midi'] and n['start'] - (merged[-1]['start'] + merged[-1]['dur']) < 0.12:
                merged[-1]['dur'] = n['start'] + n['dur'] - merged[-1]['start']  # a re-attack of the same pitch (ducking pump)
                merged[-1]['pumps'] += 1
            else:
                merged.append(dict(n, pumps=1))
        bass_rows.append(dict(raw=win['bass'], merged=merged, kicks=kicks, origin=origin,
                              bars=max(1, len(played))))
    out['bars'] = nbars
    if nbars == 0:
        return out
    for lane, short in (('Kick', 'k'), ('Snare', 's'), ('HiHat', 'h')):
        bars = lanes[lane]
        prof = [0] * 16
        for b in bars:
            for s in b:
                prof[s] += 1
        out[short + '_per_bar'] = sum(len(b) for b in bars) / len(bars)
        out[short + '_prof'] = [p / len(bars) for p in prof]
        out[short + '_distinct'] = len(set(bars))
        # Repetition: bar equal to the bar before / two before.
        same1 = sum(1 for i in range(1, len(bars)) if bars[i] == bars[i - 1])
        same2 = sum(1 for i in range(2, len(bars)) if bars[i] == bars[i - 2])
        out[short + '_rep1'] = same1 / max(1, len(bars) - 1)
        out[short + '_rep2'] = same2 / max(1, len(bars) - 2)
    kb = lanes['Kick']
    # Syncopation: kicks off the quarter notes, snares off 2 / 4.
    k_all = sum(len(b) for b in kb)
    out['kick_offbeat'] = sum(1 for b in kb for s in b if s % 4) / max(1, k_all)
    s_all = sum(len(b) for b in lanes['Snare'])
    out['snare_off_backbeat'] = sum(1 for b in lanes['Snare'] for s in b if s not in (4, 12)) / max(1, s_all)
    # Share of drum bars (kick + snare together) that occur only once in the track.
    ks = collections.Counter(zip(kb, lanes['Snare']))
    out['unique_bars'] = sum(1 for n in ks.values() if n == 1) / max(1, len(kb))
    out['four_floor'] = sum(1 for b in kb if {0, 4, 8, 12} <= b) / len(kb)
    sb = lanes['Snare']
    out['snare_backbeat'] = sum(1 for b in sb if {4, 12} <= b) / len(sb)
    out['snare_halftime'] = sum(1 for b in sb if 8 in b and 4 not in b and 12 not in b) / len(sb)
    hp = out['h_prof']
    total = sum(hp) or 1
    out['hat_8th_share'] = sum(hp[i] for i in range(0, 16, 2)) / total
    out['hat_offbeat_8th'] = sum(hp[i] for i in (2, 6, 10, 14)) / total
    out['hat_offgrid_ms'] = st.median(hat_offgrid) if hat_offgrid else 0
    out['swing'] = st.median(swing) if swing else 50
    out['key'] = collections.Counter(keys).most_common(1)[0][0] if keys else ''
    # Bass.
    notes_bar, changes_bar, dur_beats, intervals, register = [], [], [], [], []
    rel = collections.Counter()
    kicks_with_bass = kicks_total = 0
    pumps = []
    for row in bass_rows:
        merged = [n for n in row['merged'] if n['start'] >= row['origin'] - 0.05]
        if not merged:
            continue
        notes_bar.append(len(merged) / row['bars'])
        changes = sum(1 for a, b in zip(merged, merged[1:]) if a['midi'] != b['midi'])
        changes_bar.append(changes / row['bars'])
        for n in merged:
            dur_beats.append(n['dur'] / beat)
            register.append(n['midi'])
            pumps.append(n['pumps'])
        for a, b in zip(merged, merged[1:]):
            intervals.append(b['midi'] - a['midi'])
        ks = row['kicks']
        for n in merged:
            # Nearest kick; a ducked note sits 0-160 ms after its kick.
            if not ks:
                rel['no kicks'] += 1
                continue
            dt = min((n['start'] - k for k in ks), key=abs)
            if -0.04 <= dt <= 0.16:
                rel['on kick'] += 1
            elif dt < -0.04 and dt >= -2 * st16 - 0.04:
                rel['before kick (<= 2 16ths)'] += 1
            elif dt > 0.16 and dt <= 2 * st16 + 0.16:
                rel['after kick (<= 2 16ths)'] += 1
            else:
                rel['away from kicks'] += 1
        for k in ks:
            if k < row['origin'] - 0.05:
                continue
            kicks_total += 1
            if any(-0.04 <= n['start'] - k <= 0.16 for n in merged):
                kicks_with_bass += 1
    if notes_bar:
        out['bass_notes_bar'] = st.mean(notes_bar)
        out['bass_changes_bar'] = st.mean(changes_bar)
        out['bass_dur_beats'] = st.median(dur_beats)
        out['bass_long_share'] = sum(1 for x in dur_beats if x >= 2) / len(dur_beats)
        out['bass_register'] = st.median(register)
        out['bass_pumps'] = st.mean(pumps)
        n = sum(rel.values())
        out['bass_rel'] = {k: v / n for k, v in rel.items()}
        out['kicks_with_bass'] = kicks_with_bass / max(1, kicks_total)
        iv = collections.Counter()
        for x in intervals:
            a = abs(x)
            iv['repeat' if a == 0 else 'step 1-2' if a <= 2 else 'third 3-4' if a <= 4 else 'fourth/fifth 5-7'
               if a <= 7 else 'octave 12' if a == 12 else 'other leap']+= 1
        n = max(1, sum(iv.values()))
        out['bass_intervals'] = {k: v / n for k, v in iv.items()}
    if ducks:
        out['duck_db'] = st.median(x['depth'] for x in ducks)
        out['duck_ms'] = st.median(x['recover_ms'] for x in ducks)
    return out


def report(out_dir, md_path=None):
    rows = []
    for f in sorted(os.listdir(out_dir)):
        if f.endswith('.json'):
            rows.append(track_stats(json.load(open(os.path.join(out_dir, f), encoding='utf-8'))))
    rows = [r for r in rows if r.get('bars')]
    lines = []
    p = lines.append
    p(f'tracks {len(rows)}, bars {sum(r["bars"] for r in rows)}')
    p('')
    p('| track | bpm | conf | K/bar | S/bar | H/bar | 4otf | backbeat | half-time | hat 8ths | swing | K rep1 | bass notes/bar | changes/bar | dur beats | on kick | kicks w/ bass | duck dB / ms | key |')
    p('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for r in rows:
        rel = r.get('bass_rel', {})
        p(f'| {r["name"]} | {r["bpm"]:.0f} | {r["conf"]:.2f} | {r["k_per_bar"]:.1f} | {r["s_per_bar"]:.1f} | {r["h_per_bar"]:.1f} '
          f'| {r["four_floor"]:.2f} | {r["snare_backbeat"]:.2f} | {r["snare_halftime"]:.2f} | {r["hat_8th_share"]:.2f} | {r["swing"]:.0f} '
          f'| {r["k_rep1"]:.2f} | {r.get("bass_notes_bar", 0):.1f} | {r.get("bass_changes_bar", 0):.1f} | {r.get("bass_dur_beats", 0):.2f} '
          f'| {rel.get("on kick", 0):.2f} | {r.get("kicks_with_bass", 0):.2f} | {r.get("duck_db", 0):.0f} / {r.get("duck_ms", 0):.0f} | {r.get("key", "")} |')
    p('')
    # The two most common kick + snare bars of each track (16ths 1-16): shows the family, and a grid
    # an 8th / 16th off (kicks on 3-7-11-15 or 2-6-10-14 = a straight kick read with the wrong beat 1).
    p('| track | most common bars (kick / snare, 16ths 1-16) |')
    p('|---|---|')
    for f in sorted(os.listdir(out_dir)):
        if not f.endswith('.json'):
            continue
        d = json.load(open(os.path.join(out_dir, f), encoding='utf-8'))
        pats = collections.Counter()
        for w in d['windows']:
            k, s = bars_of(w, 'Kick'), bars_of(w, 'Snare')
            for b in sorted(set(k) | set(s)):
                pats[(tuple(sorted(x + 1 for x in k.get(b, ()))), tuple(sorted(x + 1 for x in s.get(b, ()))))] += 1
        if pats:
            p(f'| {d["name"]} | ' + '; '.join(f'K {list(a)} S {list(b)} x{n}' for (a, b), n in pats.most_common(2)) + ' |')
    p('')
    # Profiles by groups.
    def group(r):
        if r['conf'] < 0.5:
            return 'tempo unsure'
        if r['four_floor'] >= 0.5:
            return 'four-on-the-floor'
        if r['snare_halftime'] >= 0.5:
            return 'half-time (snare on 3)'
        if r['snare_backbeat'] >= 0.5:
            return 'backbeat (snare on 2 and 4)'
        return 'other / broken'
    groups = collections.defaultdict(list)
    for r in rows:
        groups[group(r)].append(r)
    for g, rs in sorted(groups.items(), key=lambda x: -len(x[1])):
        bars = sum(r['bars'] for r in rs)
        p(f'### {g}: {len(rs)} tracks, {bars} bars, bpm {", ".join(str(round(r["bpm"])) for r in rs)}')
        p('')
        p('| lane | ' + ' | '.join(str(i + 1) for i in range(16)) + ' | per bar |')
        p('|---|' + '---|' * 17)
        for short, lane in (('k', 'kick'), ('s', 'snare'), ('h', 'hat')):
            prof = [sum(r[short + '_prof'][i] * r['bars'] for r in rs) / bars for i in range(16)]
            per = sum(r[short + '_per_bar'] * r['bars'] for r in rs) / bars
            p(f'| {lane} | ' + ' | '.join(f'{x:.2f}' for x in prof) + f' | {per:.2f} |')
        p('')
        b = [r for r in rs if 'bass_notes_bar' in r]
        if b:
            rel = collections.Counter()
            iv = collections.Counter()
            for r in b:
                for k, v in r['bass_rel'].items():
                    rel[k] += v / len(b)
                for k, v in r['bass_intervals'].items():
                    iv[k] += v / len(b)
            p(f'bass (mean of tracks): notes/bar {st.mean(r["bass_notes_bar"] for r in b):.2f}, pitch changes/bar '
              f'{st.mean(r["bass_changes_bar"] for r in b):.2f}, median length {st.median(r["bass_dur_beats"] for r in b):.2f} beats, '
              f'notes >= 2 beats {st.mean(r["bass_long_share"] for r in b):.2f}, kicks with a bass note {st.mean(r["kicks_with_bass"] for r in b):.2f}')
            p('')
            p('bass start vs nearest kick: ' + ', '.join(f'{k} {v:.2f}' for k, v in rel.most_common()))
            p('')
            p('bass intervals: ' + ', '.join(f'{k} {v:.2f}' for k, v in iv.most_common()))
            p('')
            dk = [r for r in b if 'duck_db' in r]
            if dk:
                p(f'ducking under kicks: median {st.median(r["duck_db"] for r in dk):.1f} dB, back within 3 dB after '
                  f'{st.median(r["duck_ms"] for r in dk):.0f} ms; tracks ducking >= 6 dB: {sum(1 for r in dk if r["duck_db"] >= 6)} / {len(dk)}')
            p('')
    text = '\n'.join(lines)
    print(text)
    if md_path:
        open(md_path, 'w', encoding='utf-8').write(text + '\n')


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    if args[0] == 'run':
        tracks = [t.strip() for t in open(args[1], encoding='utf-8') if t.strip()]
        jobs = int(args[args.index('--jobs') + 1]) if '--jobs' in args else 2
        os.makedirs(args[3], exist_ok=True)
        with cf.ThreadPoolExecutor(jobs) as pool:
            for name, status in pool.map(lambda t: one(t, args[2], args[3]), tracks):
                print(name, '|', status, flush=True)
        return 0
    if args[0] == 'report':
        report(args[1], args[args.index('--md') + 1] if '--md' in args else None)
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main())
