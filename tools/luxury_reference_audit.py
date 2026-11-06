"""Local LuxuryTrap corpus audit. Originals are read only; no network/model required.

python tools/luxury_reference_audit.py --source <folder> --ffmpeg <ffmpeg.exe>
Results cache by SHA256 + analysis version. HF detections are candidates, not isolated hats.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import subprocess
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, stft, find_peaks, correlate

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from atmos_passport import parse_hits, parse_bass

VERSION = 1
SR = 22050


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def decode(ffmpeg, path, start=None, seconds=None, sr=SR):
    cmd = [ffmpeg, '-v', 'error', '-nostdin']
    if start is not None:
        cmd += ['-ss', str(start)]
    cmd += ['-i', str(path)]
    if seconds is not None:
        cmd += ['-t', str(seconds)]
    data = subprocess.check_output(cmd + ['-ac', '1', '-ar', str(sr), '-f', 'f32le', '-'])
    return np.frombuffer(data, dtype='<f4').copy()


def pitch_frames(x, sr):
    """ACF period + normalized periodicity; ignore transient and weak tail frames."""
    x = sosfiltfilt(butter(4, [25, 450], btype='bandpass', fs=sr, output='sos'), x)
    size, hop = round(sr * .16), round(sr * .04)
    rows = []
    peak = max(np.sqrt(np.mean(x*x)), 1e-9)
    for a in range(round(sr*.08), len(x)-size, hop):
        y = x[a:a+size].astype(float)
        if np.sqrt(np.mean(y*y)) < peak*.18:
            continue
        y -= y.mean()
        ac = correlate(y, y, mode='full', method='fft')[size-1:]
        # Unbiased ACF compensates finite-window attenuation at low bass frequencies.
        ac /= np.arange(size, 0, -1)
        lo, hi = int(sr/180), min(int(sr/28), size-2)
        peaks, _ = find_peaks(ac[lo:hi])
        if not len(peaks) or ac[0] <= 0:
            continue
        peaks += lo
        p = int(peaks[np.argmax(ac[peaks])])
        q = float(ac[p]/ac[0])
        if q < .72:
            continue
        delta = .5*(ac[p-1]-ac[p+1])/(ac[p-1]-2*ac[p]+ac[p+1])
        hz = sr/(p+delta)
        rows.append(dict(time=a/sr, hz=float(hz), midi=float(69+12*np.log2(hz/440)), periodicity=q))
    return rows


def hat_candidates(x, sr, delta):
    _, t, z = stft(x, sr, nperseg=256, noverlap=192, boundary=None)
    mag = np.abs(z[int(6000/(sr/256)):])
    flux = np.maximum(0, np.diff(mag, axis=1)).sum(axis=0)
    if not len(flux) or flux.max() <= 0:
        return []
    flux /= flux.max()
    peaks, _ = find_peaks(flux, height=delta, prominence=delta*.65, distance=4)
    return t[1:][peaks].tolist()


def gaps(times, bpm):
    counts = collections.Counter()
    run = rolls = 0
    for gap in np.diff(times):
        r = gap/(60/bpm/4)
        label = 'other'
        for name, value in [('1/64', .25), ('1/32T', 1/3), ('1/32', .5), ('1/16T', 2/3), ('1/16', 1), ('1/8T', 4/3), ('1/8', 2)]:
            if abs(r-value) <= value*.15:
                label = name
                break
        counts[label] += 1
        if label in ('1/64', '1/32T', '1/32', '1/16T'):
            run += 1
        else:
            rolls += int(run >= 3)
            run = 0
    rolls += int(run >= 3)
    return dict(counts), rolls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--ffmpeg', required=True)
    ap.add_argument('--out', type=Path, default=ROOT/'quality-lab/luxurytrap-2026-11')
    args = ap.parse_args()
    cache = ROOT/'build/luxury-reference'
    cache.mkdir(parents=True, exist_ok=True)
    lab = ROOT/'build/Release/HPDG_BreakLab.exe'
    inventory, tracks, samples = [], [], []
    paths = sorted([*args.source.glob('StEMS/*.mp3'), *args.source.glob('oneshots/**/*.wav')])
    for path in paths:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        inventory.append(dict(path=str(path), sha256=digest, bytes=path.stat().st_size))
        result = cache/f'{digest}-v{VERSION}.json'
        if result.exists():
            row = json.loads(result.read_text(encoding='utf-8'))
        elif path.suffix == '.wav':
            x = decode(args.ffmpeg, path)
            row = dict(name=path.name, category=path.parent.name, seconds=len(x)/SR,
                       peak=float(np.max(np.abs(x))), sha256=digest)
            if path.parent.name == '808':
                frames = pitch_frames(x, SR)
                values = np.array([f['midi'] for f in frames])
                row['pitch_frames'] = frames
                if len(values):
                    midi = float(np.median(values))
                    row.update(midi=midi, hz=float(440*2**((midi-69)/12)),
                               spread_semitones=float(np.percentile(values,90)-np.percentile(values,10)),
                               semitones_to_C2=36-midi)
            save(result, row)
        else:
            x = decode(args.ffmpeg, path)
            duration = len(x)/SR
            row = dict(name=path.name, sha256=digest, seconds=duration, windows=[])
            for wi, frac in enumerate((.30, .60)):
                start = duration*frac
                cut = x[round(start*SR):round((start+24)*SR)]
                wav = cache/f'{digest}-w{wi}.wav'
                wavfile.write(wav, SR, cut)
                command = ['analyze', str(wav), '--cut', '--hits'] if '_(Drums)' in path.name else ['lines', str(wav), '--print']
                raw = subprocess.check_output([str(lab), *command], stdin=subprocess.DEVNULL).decode('utf-8', errors='replace')
                (cache/f'{digest}-w{wi}.txt').write_text(raw, encoding='utf-8')
                w = dict(start=start, seconds=len(cut)/SR)
                if '_(Drums)' in path.name:
                    head, hits = parse_hits(raw)
                    if not head:
                        raise RuntimeError(f'No tempo: {path}')
                    w.update(tempo=head, hits=hits)
                    w['hf_onsets'] = {str(d):hat_candidates(cut, SR, d) for d in (.06,.10,.16)}
                else:
                    notes, key = parse_bass(raw)
                    w.update(notes=notes, key=key)
                row['windows'].append(w)
            save(result, row)
        # Identical audio may have multiple names; cache identity must not rename inputs.
        row['name'] = path.name
        (samples if path.suffix == '.wav' else tracks).append(row)
        print(path.name, 'OK', flush=True)
    # Pair using exact shared filename, not filesystem ordering. Compare both tempo hypotheses.
    drums = {r['name'].replace('_(Drums)', ''):r for r in tracks if '_(Drums)' in r['name']}
    for bass in [r for r in tracks if '_(Bass)' in r['name']]:
        drum = drums[bass['name'].replace('_(Bass)', '')]
        for bw, dw in zip(bass['windows'], drum['windows']):
            native = dw['tempo']['bpm']
            bpm = min((native*.5,native,native*2), key=lambda b:abs(np.log2(b/145)))
            dw['display_bpm_hypothesis'] = bpm
            bars = dw['seconds']*bpm/240
            dw['hat_metrics'] = {}
            for threshold,times in dw['hf_onsets'].items():
                counts, rolls = gaps(times,bpm)
                dw['hat_metrics'][threshold] = dict(candidates_per_bar=len(times)/bars, gap_counts=counts, rolls_per_4bars=rolls/bars*4)
            notes = sorted(bw['notes'], key=lambda n:n['start'])
            kicks = [h['t'] for h in dw['hits'] if h['lane']=='Kick' and h['conf']>=.5]
            # Different stem lengths imply different absolute cut points: retain correction.
            offset = bw['start']-dw['start']
            bw['metrics'] = dict(bpm_hypothesis=bpm, notes_per_bar=len(notes)/bars,
                median_duration_beats=float(np.median([n['dur']*bpm/60 for n in notes])) if notes else None,
                same_pitch_share=sum(a['midi']==b['midi'] for a,b in zip(notes,notes[1:]))/max(1,len(notes)-1),
                kick_alignment_60ms=sum(min((abs(n['start']+offset-k) for k in kicks),default=999)<=.06 for n in notes)/max(1,len(notes)),
                stem_offset_seconds=offset)
    save(args.out/'inventory.json', inventory)
    save(args.out/'analysis.json', dict(version=VERSION, tracks=tracks, samples=samples,
        limitations=['HF onsets include snares/open hats/separation artifacts; not confirmed hats.',
                      'Tempo is a hypothesis nearest 145 BPM; half/double time requires listening.',
                      'Bass transcription may merge same-pitch attacks and miss slides.',
                      'Two 24-second windows do not cover all beat switches.',
                      'ACF pitch can have octave/subharmonic errors; inspect stability before tuning.']))
    print('SAVED', args.out, flush=True)


if __name__ == '__main__':
    main()
