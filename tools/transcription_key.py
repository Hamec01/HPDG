"""Key from transcribed notes: Spotify Basic Pitch (Apache-2.0) against HPDG's own key (stage 6).

Lab only: Basic Pitch runs from a venv outside the repo; the notes it writes are cached outside the repo
(third-party audio stays out of git). Same files, same truth and same scoring as the key bench
(`tempo_bench.py run-key` / `key-report`).

usage:
  <venv python> -I tools/transcription_key.py run <corpus.tsv> <out folder>
      Basic Pitch on every tonal corpus file -> <out>/notes/<id>/<file>.csv (start, end, midi, amplitude)
      and <out>/timing.csv (seconds of audio, seconds of inference). Existing note files are kept.
  python tools/transcription_key.py report <corpus.tsv> <out folder> <plugin harmony folder> [--json <file>]
      Key accuracy per arm (MIREX weighted: exact 1, fifth 0.5, relative 0.3, parallel 0.2):
        C   the plugin (its harmony reports: `tempo_bench.py run-key`)
        B0  Basic Pitch notes, pitch-class histogram (duration x amplitude), the plugin's
            Albrecht-Shanahan profiles and minor prior 0.1
        B1  B0 + the plugin's bass bonus (0.2 x share of the lowest sounding note's pitch class)
        B2  B1 + the plugin's opening bonus (0.5 on the first bass note's pitch class)
      and the inference speed (x real time, p50 / p95 / worst seconds per 10 s of audio).
  python tools/transcription_key.py files <corpus.tsv> <out .tsv>
      the same tonal files as corpus id, file name, full path: the input of HPDG's C++ port
      (`HPDG_BasicPitchLab corpus`), which writes the same notes / timing layout, so `report` reads both
"""
import csv
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I leaves the script folder out
import tempo_bench as tb  # noqa: E402

# the plugin's profiles (SampleHarmonyAnalyzer.cpp)
MAJOR_AS = [0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081]
MINOR_AS = [0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052]


def tonal_files(corpus_path):
    for corpus in tb.load_corpus(corpus_path):
        if corpus['category'] != 'tonal':
            continue
        for name, _ in tb.files(corpus):
            if tb.key_from_name(name) is not None:
                yield corpus, name


def run(corpus_path, out):
    from basic_pitch import ICASSP_2022_MODEL_PATH, FilenameSuffix, build_icassp_2022_model_path
    from basic_pitch.inference import Model, predict
    start = time.perf_counter()
    model = Model(build_icassp_2022_model_path(FilenameSuffix.onnx))
    print(f'model load {time.perf_counter() - start:.2f} s ({ICASSP_2022_MODEL_PATH})', flush=True)
    timing_path = os.path.join(out, 'timing.csv')
    os.makedirs(out, exist_ok=True)
    new_timing = not os.path.exists(timing_path)
    with open(timing_path, 'a', newline='', encoding='utf-8') as timing:
        writer = csv.writer(timing)
        if new_timing:
            writer.writerow(['corpus', 'file', 'audio_s', 'infer_s'])
        for corpus, name in tonal_files(corpus_path):
            target = os.path.join(out, 'notes', corpus['id'], name + '.csv')
            if os.path.exists(target):
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            path = tb.path_of(corpus, name)
            began = time.perf_counter()
            try:
                model_output, _, notes = predict(path, model)
            except Exception as error:  # an unreadable file: skip it, say so
                print(f'  cannot read {name}: {error}', flush=True)
                continue
            seconds = time.perf_counter() - began
            audio = len(model_output['note']) / (22050 / 256)  # 86.13 frames per second
            with open(target, 'w', newline='', encoding='utf-8') as f:
                w = csv.writer(f)
                w.writerow(['start', 'end', 'midi', 'amplitude'])
                for s, e, pitch, amp, _ in notes:
                    w.writerow([f'{s:.4f}', f'{e:.4f}', int(pitch), f'{amp:.4f}'])
            writer.writerow([corpus['id'], name, f'{audio:.3f}', f'{seconds:.3f}'])
            timing.flush()
            print(f'  {corpus["id"]:12s} {seconds:6.2f} s for {audio:5.1f} s  {len(notes):4d} notes  {name}', flush=True)


def correlation(chroma, profile, root):
    mc = sum(chroma) / 12.0
    mp = sum(profile) / 12.0
    num = cv = pv = 0.0
    for pc in range(12):
        c = chroma[pc] - mc
        p = profile[(pc - root) % 12] - mp
        num += c * p
        cv += c * c
        pv += p * p
    return num / (cv * pv) ** 0.5 if cv * pv > 1e-18 else 0.0


def key_from_notes(notes, bass_weight, opening_weight):
    chroma = [0.0] * 12
    bass = [0.0] * 12
    for s, e, m, a in notes:
        chroma[m % 12] += (e - s) * a
    # the lowest note sounding at each note start = the bass there
    starts = sorted(notes)
    opening = -1
    for s, e, m, a in starts:
        sounding = [n for n in starts if n[0] <= s + 0.02 and n[1] > s]
        low = min(sounding, key=lambda n: n[2])
        if low[2] == m:
            bass[m % 12] += (e - s) * a
            if opening < 0 and m < 60:
                opening = m % 12
    total = sum(chroma)
    if total <= 0:
        return None, 0.0
    peak = max(chroma)
    chroma = [c / peak for c in chroma]
    bass_total = sum(bass)
    scores = []
    for root in range(12):
        for mode in (0, 1):
            score = correlation(chroma, MAJOR_AS if mode else MINOR_AS, root)
            if bass_total > 0:
                score += bass_weight * bass[root] / bass_total
            if root == opening:
                score += opening_weight
            if mode == 0:
                score += 0.10
            scores.append((score, root, mode))
    scores.sort(reverse=True)
    return (scores[0][1], scores[0][2]), scores[0][0] - scores[1][0]


def read_notes(path):
    with open(path, encoding='utf-8') as f:
        return [(float(r['start']), float(r['end']), int(r['midi']), float(r['amplitude'])) for r in csv.DictReader(f)]


def plugin_key(harmony, corpus, name):
    path = os.path.join(harmony, 'harmony', corpus['id'], name + '.txt')
    if not os.path.exists(path):
        return None
    m = tb.KEY_FOUND.search(open(path, encoding='utf-8', errors='replace').read())
    if not m:
        return None
    root = (tb.NOTE[m.group(1)[0]] + (1 if m.group(1).endswith('#') else 0)) % 12
    return root, 1 if m.group(2) == 'major' else 0


def summary(label, kinds):
    n = len(kinds)
    share = lambda k: sum(1 for x in kinds if x == k) / n
    s = dict(n=n, mirex=sum(tb.MIREX[k] for k in kinds) / n, exact=share('exact'), fifth=share('fifth'),
             relative=share('relative'), parallel=share('parallel'), other=share('other'))
    print(f"{label:6s} n {n:3d} | MIREX {s['mirex']:.3f} | exact {s['exact']:.3f} fifth {s['fifth']:.3f} "
          f"relative {s['relative']:.3f} parallel {s['parallel']:.3f} other {s['other']:.3f}")
    return s


def report(corpus_path, out, harmony, json_path):
    arms = {'C': [], 'B0': [], 'B1': [], 'B2': []}
    per_pack = {}
    for corpus, name in tonal_files(corpus_path):
        notes_path = os.path.join(out, 'notes', corpus['id'], name + '.csv')
        plugin = plugin_key(harmony, corpus, name)
        if not os.path.exists(notes_path) or plugin is None:
            continue  # the same files in every arm
        truth = tb.key_from_name(name)
        notes = read_notes(notes_path)
        found = {
            'C': plugin,
            'B0': key_from_notes(notes, 0.0, 0.0)[0],
            'B1': key_from_notes(notes, 0.2, 0.0)[0],
            'B2': key_from_notes(notes, 0.2, 0.5)[0],
        }
        for arm, key in found.items():
            kind = tb.key_kind(key, truth) if key is not None else 'other'
            arms[arm].append(kind)
            per_pack.setdefault(corpus['id'], {}).setdefault(arm, []).append(kind)
    result = {'ALL': {arm: summary(arm, kinds) for arm, kinds in arms.items() if kinds}}
    for pack, by_arm in per_pack.items():
        print(f'-- {pack}')
        result[pack] = {arm: summary(arm, kinds) for arm, kinds in by_arm.items()}

    timing_path = os.path.join(out, 'timing.csv')
    if os.path.exists(timing_path):
        rows = list(csv.DictReader(open(timing_path, encoding='utf-8')))
        per10 = sorted(10.0 * float(r['infer_s']) / max(0.1, float(r['audio_s'])) for r in rows)
        audio = sum(float(r['audio_s']) for r in rows)
        infer = sum(float(r['infer_s']) for r in rows)
        pick = lambda q: per10[min(len(per10) - 1, int(q * len(per10)))]
        speed = dict(files=len(rows), realtime=audio / infer, p50=pick(0.5), p95=pick(0.95), worst=per10[-1])
        print(f"speed: {speed['files']} files, {speed['realtime']:.1f}x real time | per 10 s of audio "
              f"p50 {speed['p50']:.2f} s, p95 {speed['p95']:.2f} s, worst {speed['worst']:.2f} s")
        result['speed'] = speed
    if json_path:
        json.dump(result, open(json_path, 'w', encoding='utf-8'), indent=1)


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) >= 3 and args[0] == 'run':
        run(args[1], args[2])
    elif len(args) >= 3 and args[0] == 'files':
        with open(args[2], 'w', encoding='utf-8', newline='\n') as f:
            for corpus, name in tonal_files(args[1]):
                f.write(f"{corpus['id']}\t{name}\t{tb.path_of(corpus, name)}\n")
    elif len(args) >= 4 and args[0] == 'report':
        report(args[1], args[2], args[3], args[args.index('--json') + 1] if '--json' in args else None)
    else:
        print(__doc__)
