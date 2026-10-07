"""Drum reference from MIDI loops (roadmap Phase 2): exact notes, no transcription.

usage: python tools/midi_drums.py <kick.mid> [...]          prints per-bar 16th positions of every note
       (import) read_onsets(path) -> (ppq, [tick, ...])     note-on ticks (velocity > 0), any pitch

Minimal Standard MIDI File reader (format 0 / 1, running status, meta / sysex skipped); a bar is
4 beats (4/4), a 16th is ppq / 4 ticks, notes are rounded to the nearest 16th.
"""
import sys


def _vlq(data, i):
    value = 0
    while True:
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if b < 0x80:
            return value, i


def read_onsets(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'MThd', path
    ppq = int.from_bytes(data[12:14], 'big')
    i, onsets = 14, []
    while i < len(data):
        kind, length = data[i:i + 4], int.from_bytes(data[i + 4:i + 8], 'big')
        i += 8
        end = i + length
        if kind == b'MTrk':
            tick, status = 0, 0
            while i < end:
                delta, i = _vlq(data, i)
                tick += delta
                b = data[i]
                if b == 0xFF:
                    _, length2 = data[i + 1], None
                    length2, i = _vlq(data, i + 2)
                    i += length2
                    continue
                if b in (0xF0, 0xF7):
                    length2, i = _vlq(data, i + 1)
                    i += length2
                    continue
                if b & 0x80:
                    status = b
                    i += 1
                kind2 = status & 0xF0
                size = 1 if kind2 in (0xC0, 0xD0) else 2
                args = data[i:i + size]
                i += size
                if kind2 == 0x90 and args[1] > 0:
                    onsets.append((tick, args[0], args[1]))
        i = end
    return ppq, sorted(onsets)


def bars_of(path):
    """16th positions per bar (set per bar), bars counted over the loop length (whole bars)."""
    ppq, notes = read_onsets(path)
    step = ppq / 4
    steps = [int(round(t / step)) for t, _, _ in notes]
    if not steps:
        return []
    nbars = max(1, -(-(max(steps) + 1) // 16))
    bars = [set() for _ in range(nbars)]
    for s in steps:
        bars[(s // 16) % nbars].add(s % 16)
    return bars


if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(p, ' | '.join(' '.join(map(str, sorted(b))) for b in bars_of(p)))
