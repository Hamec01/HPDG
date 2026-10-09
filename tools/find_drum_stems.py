"""Find drum-stem sets (one-shot folders skipped; output: folder TAB kick TAB snare TAB hat) (kick + snare/clap + hat of one loop or kit) in sample-pack folders, for the
K/S/H benchmark. Prints one candidate per folder / kit prefix with its files.
  python tools/find_drum_stems.py <root> [<root> ...]"""
import os
import re
import sys

ROLE = (
    ('kick', re.compile(r'(?<![a-z])(kick|kik|bd)(?![a-z])', re.I)),
    ('snare', re.compile(r'(?<![a-z])(snare|clap|snr|sd)(?![a-z])', re.I)),
    ('hat', re.compile(r'(?<![a-z])(hi ?-?hats?|hats?|hh|tops?|cymbals?)(?![a-z])', re.I)),
)
FULL = re.compile(r'(?<![a-z])(full|drums?|all|mix|beat)(?![a-z])', re.I)


def role_of(name):
    found = [r for r, rx in ROLE if rx.search(name)]
    return found[0] if len(found) == 1 else None


def kit_key(name, role):
    """The file name without its role word: files of one kit share it."""
    stem = os.path.splitext(name)[0]
    for r, rx in ROLE:
        if r == role:
            stem = rx.sub('#', stem, count=1)
    return re.sub(r'\s+', ' ', stem).strip(' -_').lower()


def main(roots):
    total = 0
    for root in roots:
        for d, _, names in os.walk(root):
            if 'one-shot' in d.lower() or 'one shot' in d.lower() or 'oneshot' in d.lower():
                continue
            wavs = [n for n in names if n.lower().endswith(('.wav', '.aif', '.aiff'))]
            if len(wavs) < 3:
                continue
            kits = {}
            for n in wavs:
                r = role_of(n)
                if r:
                    kits.setdefault(kit_key(n, r), {}).setdefault(r, []).append(n)
            for key, roles in kits.items():
                if all(r in roles for r in ('kick', 'snare', 'hat')):
                    total += 1
                    print('\t'.join([d.replace(os.sep, '/'), roles['kick'][0], roles['snare'][0], roles['hat'][0]]))
            # folder-level set (one kit per folder, e.g. ".../drums/kick (x).wav")
            if not any(all(r in roles for r in ('kick', 'snare', 'hat')) for roles in kits.values()):
                per = {r: [n for n in wavs if role_of(n) == r] for r, _ in ROLE}
                if all(len(v) == 1 for v in per.values()):
                    total += 1
                    print('\t'.join([d.replace(os.sep, '/'), per['kick'][0], per['snare'][0], per['hat'][0]]))
    print('# sets found:', total)


if __name__ == '__main__':
    main(sys.argv[1:])
