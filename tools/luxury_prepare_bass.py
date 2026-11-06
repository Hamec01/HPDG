"""Create verified C-tuned COPIES at the nearest C octave; preserve originals.

Requires completed luxury_reference_audit.py. Sampler neutral trigger MIDI 60 (owner's
FL C5 convention) is distinct from the waveform's actual C1/C2 fundamental.
"""
from pathlib import Path
import argparse
import hashlib
import json
from fractions import Fraction
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly
from luxury_reference_audit import decode, pitch_frames, save


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--ffmpeg', required=True)
    ap.add_argument('--audit', type=Path, default=Path('quality-lab/luxurytrap-2026-11/analysis.json'))
    ap.add_argument('--out', type=Path, default=Path('build/luxury-reference/C-tuned-808'))
    args = ap.parse_args()
    rows = json.loads(args.audit.read_text(encoding='utf-8'))['samples']
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for row in rows:
        if row['category'] != '808':
            continue
        src = args.source/'oneshots/808'/row['name']
        assert hashlib.sha256(src.read_bytes()).hexdigest() == row['sha256']
        if 'midi' not in row or row['spread_semitones'] > .7:
            raise ValueError(f"Unstable source: {src}")
        target = int(12*round(row['midi']/12))
        ratio = 2**((target-row['midi'])/12)
        # Resample as sampler tuning, keeping all decoded source channels at source rate.
        sr, original = wavfile.read(src)
        if np.issubdtype(original.dtype, np.integer):
            if original.dtype == np.uint8:
                x = (original.astype(float)-128)/128
            else:
                x = original.astype(float)/(np.iinfo(original.dtype).max+1)
        else:
            x = original.astype(float)
        frac = Fraction(1/ratio).limit_denominator(20000)
        y = resample_poly(x, frac.numerator, frac.denominator, axis=0)
        gain = min(1., .98/max(float(np.max(np.abs(y))),1e-9))
        y *= gain
        dst = args.out/f"{src.stem} - C{target//12-1}.wav"
        wavfile.write(dst, sr, y.astype(np.float32))
        frames = pitch_frames(decode(args.ffmpeg, dst), 22050)
        if not frames:
            raise ValueError(f'No verification pitch: {dst}')
        measured = float(np.median([f['midi'] for f in frames]))
        cents = (measured-target)*100
        # A source transient/decay can move the measured median after a rate change.
        # Reject rather than hide a mismatch in a generated asset.
        if abs(cents) > 12:
            raise ValueError(f'Pitch verification failed {dst}: {cents:.2f} cents')
        manifest.append(dict(source=str(src), source_sha256=row['sha256'], output=str(dst.resolve()),
            output_sha256=hashlib.sha256(dst.read_bytes()).hexdigest(),
            physical_root_midi=target, physical_root_hz=440*2**((target-69)/12),
            neutral_sampler_trigger_midi=60, measured_cents=cents,
            tuning_semitones=target-row['midi'], rate=ratio, gain=gain,
            note='Nearest C octave; FL C5 neutral trigger is a sampler mapping, not waveform frequency.'))
        print(src.name, f'{cents:+.2f} cents', flush=True)
    save(args.out/'manifest.json', manifest)
    print('Verified', len(manifest), 'C-tuned copies')


if __name__ == '__main__':
    main()
