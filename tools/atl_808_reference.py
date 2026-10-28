"""What an ATL 808 sounds like: its notes measured on Demucs bass stems (for the ATL808 voice).

usage: python tools/atl_808_reference.py <stems root> [--csv out.csv]

<stems root>/<track>/bass.flac (Demucs htdemucs). Per 808 note (an onset of the low band, notes
shorter than 0.25 s skipped):
  - f0          steady pitch, 80-330 ms after the onset (FFT peak 25-110 Hz);
  - drop        pitch of the first two periods against f0, semitones (the "punch" glide);
  - h2 / h3     2nd / 3rd harmonic against the fundamental, dB (saturation / distortion);
  - t-12        seconds until the note falls 12 dB below its peak (decay), capped by the next note;
  - sustained   share of notes that never fall 12 dB before the next note (held 808s).
Numbers only; the audio stays out of git.
"""
import os
import statistics as st
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

SR = 22050


def load(path):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != SR:
        import librosa
        x = librosa.resample(x, orig_sr=sr, target_sr=SR)
    return x


def lowpass(x, hz):
    return sosfiltfilt(butter(4, hz, 'low', fs=SR, output='sos'), x)


def envelope_db(x, hop, window_s=0.04):
    """RMS over 40 ms (longer than one period of a 25 Hz 808) every `hop` samples: shorter frames
    ripple +-3 dB with the wave itself and read every period of a low note as a new attack."""
    w = int(window_s * SR)
    power = np.convolve(x ** 2, np.ones(w) / w, mode='full')[w - 1:]  # frame i: samples i .. i + w
    frames = len(x) // hop
    rms = np.sqrt(power[:frames * hop:hop] + 1e-12)
    return 20 * np.log10(rms + 1e-9)


def onsets(env, hop):
    """Frames where the low band jumps >= 6 dB within 30 ms and is within 30 dB of the track peak."""
    peak = env.max()
    look = max(1, int(0.03 * SR / hop))
    out = []
    i = look
    while i < len(env):
        if env[i] > peak - 30 and env[i] - env[i - look] >= 6:
            j = i
            while j + 1 < len(env) and env[j + 1] > env[j]:
                j += 1
            out.append(i - look)
            i = j + int(0.12 * SR / hop)  # one onset per attack
        else:
            i += 1
    return out


def f0_of(seg):
    n = 1 << 15
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freqs = np.fft.rfftfreq(n, 1 / SR)
    band = (freqs >= 25) & (freqs <= 110)
    return freqs[band][np.argmax(spec[band])], spec, freqs


def amp_at(spec, freqs, hz):
    band = (freqs >= hz * 0.94) & (freqs <= hz * 1.06)
    return spec[band].max() if band.any() else 1e-9


def first_periods_hz(low, start, f0):
    """Frequency from the first two positive zero crossings after the attack."""
    seg = low[start:start + int(0.15 * SR)]
    zc = np.where((seg[:-1] <= 0) & (seg[1:] > 0))[0]
    if len(zc) < 4:
        return None
    period = (zc[3] - zc[1]) / 2 / SR
    return 1 / period if period > 0 else None


def analyse(path):
    x = load(path)
    low = lowpass(x, 250)
    hop = int(0.005 * SR)
    env = envelope_db(low, hop)
    marks = onsets(env, hop)
    notes = []
    for k, frame in enumerate(marks):
        start = frame * hop
        end = marks[k + 1] * hop if k + 1 < len(marks) else min(len(x), start + 3 * SR)
        if end - start < 0.25 * SR:
            continue
        steady = x[start + int(0.08 * SR):min(end, start + int(0.33 * SR))]
        if len(steady) < int(0.1 * SR):
            continue
        f0, spec, freqs = f0_of(steady)
        a1 = amp_at(spec, freqs, f0)
        h2 = 20 * np.log10(amp_at(spec, freqs, 2 * f0) / a1)
        h3 = 20 * np.log10(amp_at(spec, freqs, 3 * f0) / a1)
        lowseg = lowpass(x[start:end], 150)
        first = first_periods_hz(lowseg, int(0.004 * SR), f0)
        drop = 12 * np.log2(first / f0) if first else None
        e = env[frame:end // hop]
        p = int(np.argmax(e[:max(1, int(0.08 * SR / hop))]))
        below = np.where(e[p:] < e[p] - 12)[0]
        sustained = len(below) == 0
        t12 = (below[0] if not sustained else len(e) - p) * hop / SR
        notes.append(dict(f0=f0, h2=h2, h3=h3, drop=drop, t12=t12, sustained=sustained, length=(end - start) / SR))
    return notes


def med(values):
    values = [v for v in values if v is not None]
    return st.median(values) if values else float('nan')


def main():
    root = sys.argv[1]
    csv_path = sys.argv[sys.argv.index('--csv') + 1] if '--csv' in sys.argv else None
    rows = []
    print('| track | notes | f0 Hz | drop st | h2 dB | h3 dB | t-12 s | sustained | note s |')
    print('|---|---|---|---|---|---|---|---|---|')
    every = []
    for name in sorted(os.listdir(root)):
        path = os.path.join(root, name, 'bass.flac')
        if not os.path.exists(path):
            continue
        notes = analyse(path)
        every += notes
        if not notes:
            continue
        row = (name[:48], len(notes), med(n['f0'] for n in notes), med(n['drop'] for n in notes),
               med(n['h2'] for n in notes), med(n['h3'] for n in notes), med(n['t12'] for n in notes),
               sum(n['sustained'] for n in notes) / len(notes), med(n['length'] for n in notes))
        rows.append(row)
        print('| %s | %d | %.1f | %.1f | %.1f | %.1f | %.2f | %.2f | %.2f |' % row)
    if every:
        print('| **all** | %d | %.1f | %.1f | %.1f | %.1f | %.2f | %.2f | %.2f |' % (
            len(every), med(n['f0'] for n in every), med(n['drop'] for n in every), med(n['h2'] for n in every),
            med(n['h3'] for n in every), med(n['t12'] for n in every),
            sum(n['sustained'] for n in every) / len(every), med(n['length'] for n in every)))
    if csv_path:
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write('track,notes,f0,drop,h2,h3,t12,sustained,length\n')
            for r in rows:
                f.write(','.join(str(v) for v in r) + '\n')


if __name__ == '__main__':
    main()
