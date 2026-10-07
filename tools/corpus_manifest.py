"""Write / verify a manifest of a local audio corpus (the audio itself stays out of git).

usage:
  python tools/corpus_manifest.py write  <corpus folder> <manifest.csv> [label]
  python tools/corpus_manifest.py verify <corpus folder> <manifest.csv>

The manifest lists every audio file (relative path, bytes, SHA-256, tempo parsed from the name)
so a benchmark on another machine can prove it runs on the same material (RULE 4).
"""
import csv
import hashlib
import os
import re
import sys

AUDIO = ('.wav', '.mp3', '.flac', '.aif', '.aiff')


def tempo(name):
    m = re.search(r'([0-9]+(?:\.[0-9]+)?)\s*B?pm', name, re.I)
    return m.group(1) if m else ''


def files(root):
    for base, _, names in os.walk(root):
        for n in sorted(names):
            if n.lower().endswith(AUDIO):
                path = os.path.join(base, n)
                yield os.path.relpath(path, root).replace('\\', '/'), path


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


mode, root, manifest = sys.argv[1], sys.argv[2], sys.argv[3]
if mode == 'write':
    label = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(os.path.normpath(root))
    with open(manifest, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['corpus', 'file', 'bytes', 'sha256', 'labeled_bpm'])
        count = 0
        for rel, path in files(root):
            w.writerow([label, rel, os.path.getsize(path), sha(path), tempo(rel)])
            count += 1
    print(f'{manifest}: {count} files')
else:
    expected = {r['file']: r for r in csv.DictReader(open(manifest, encoding='utf-8'))}
    found = dict(files(root))
    missing = [k for k in expected if k not in found]
    changed = [k for k in expected if k in found and sha(found[k]) != expected[k]['sha256']]
    print(f'expected {len(expected)} | missing {len(missing)} | changed {len(changed)} | extra {len(set(found) - set(expected))}')
    for k in (missing + changed)[:20]:
        print('  ', 'MISSING' if k in missing else 'CHANGED', k)
    sys.exit(1 if missing or changed else 0)
