"""Refine neural bass-note timestamps with phase-independent waveform attack evidence."""
import numpy as np
from scipy.signal import butter, sosfiltfilt, resample_poly, hilbert, find_peaks
from scipy.ndimage import uniform_filter1d
from fractions import Fraction


def attack_times(wave, sample_rate):
    rate = 4000
    fraction = Fraction(rate, sample_rate)
    x = resample_poly(wave.astype(float), fraction.numerator, fraction.denominator)
    x = sosfiltfilt(butter(3, [30, 1500], btype='bandpass', fs=rate, output='sos'), x)
    # Analytic amplitude avoids labelling every low-frequency waveform cycle an attack.
    env = uniform_filter1d(np.abs(hilbert(x)), 32)
    rise = env - np.roll(env, 40)
    rise[:40] = 0
    peaks, info = find_peaks(rise, distance=240, prominence=np.max(env)*.025)
    times = []
    for peak in peaks:
        a = max(0, peak-240)
        minimum = a+int(np.argmin(env[a:peak]))
        increase = env[peak]-env[minimum]
        if increase < np.max(env)*.025:
            continue
        threshold = env[minimum]+increase*.15
        crossing = minimum+int(np.argmax(env[minimum:peak+1] >= threshold))
        times.append((crossing/rate, peak/rate, float(increase)))
    return times


def refine_onset(start, attacks):
    nearby = [a for a in attacks if start-.12 <= a[0] <= start+.10]
    if not nearby:
        return start, False
    # Prefer the nearest attack, not the largest kick elsewhere in the search range.
    chosen = min(nearby, key=lambda a: abs(a[0]-start))
    return chosen[0], True
