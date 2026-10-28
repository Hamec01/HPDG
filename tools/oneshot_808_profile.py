"""808 one-shots measured like the reference stems (tools/atl_808_reference.py), for comparison.

usage: python tools/oneshot_808_profile.py <folder> [<folder> ...]

Each one-shot is played at its own pitch after 0.5 s of silence; f0, 2nd / 3rd harmonic against the
fundamental and the time to fall 12 dB come from the same code as the reference measurement.
"""
import os
import sys
import tempfile

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(__file__))
import atl_808_reference as ref  # noqa: E402

print('| sample | f0 Hz | h2 dB | h3 dB | t-12 s |')
print('|---|---|---|---|---|')
tmp = tempfile.mkdtemp()
os.makedirs(os.path.join(tmp, 'x'), exist_ok=True)
for folder in sys.argv[1:]:
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith('.wav'):
            continue
        y, sr = sf.read(os.path.join(folder, name), always_2d=True)
        y = y.mean(axis=1)
        sf.write(os.path.join(tmp, 'x', 'bass.flac'), np.concatenate([np.zeros(int(0.5 * sr)), y[: int(4 * sr)]]), sr)
        notes = ref.analyse(os.path.join(tmp, 'x', 'bass.flac'))
        if not notes:
            print(f'| {os.path.basename(folder.rstrip("/"))}/{name} | - | - | - | - |')
            continue
        n = notes[0]
        print('| %s/%s | %.1f | %.1f | %.1f | %.2f |' % (os.path.basename(os.path.dirname(folder.rstrip('/'))), name, n['f0'], n['h2'], n['h3'], n['t12']))
