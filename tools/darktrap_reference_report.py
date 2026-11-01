"""Publish aggregate research data and generated listening examples, never source audio."""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics as st
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "build/darktrap-reference"
AUDIT = ROOT / "quality-lab/darktrap-2026-11"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")


def corpus():
    measured, tracks, tone = read(CACHE / "analysis.json"), read(CACHE / "tracks.json"), read(CACHE / "timbre-yin.json")
    rows = []
    for track, meta in zip(measured, tracks):
        ws = track["windows"]
        assert len(ws) == 2, f"Incomplete coverage: {track['id']}"
        gaps = collections.Counter()
        for w in ws:
            gaps.update(w["hat_gaps"])
        count = max(1, sum(gaps.values()))
        durations = [n["dur"] * w["bpm"] / 60 for w in ws for n in w["bass_notes"]]
        pitches = [n["midi"] for w in ws for n in w["bass_notes"]]
        tw = [t for t in tone if t["window"].startswith(track["id"] + "_") and t["medians"]]
        rows.append(dict(id=track["id"], file=track["name"], sha256=meta["sha256"], split=track["split"],
                         measured_seconds=sum(w["seconds"] for w in ws),
                         source_offsets_seconds=[meta["duration"] * f for f in (.28, .60)],
                         style_bpm=[w["bpm"] for w in ws], analyzer_bpm=[w["analyzed_bpm"] for w in ws],
                         kick_starts_per_bar=st.mean(w["kick_per_bar"] for w in ws),
                         bass_pitch_segments_per_bar=st.mean(w["bass_per_bar"] for w in ws),
                         bass_segment_mean_quarters=st.mean(durations), bass_segment_median_quarters=st.median(durations),
                         bass_segment_gap_fill_median=st.median(w["bass_gap_fill"] for w in ws),
                         bass_median_midi=st.median(pitches),
                         hats_per_bar=st.mean(w["hats_per_bar"] for w in ws),
                         hat_gap_fraction={k:v/count for k,v in gaps.items()},
                         hat_rolls_per_four_bars=sum(w["rolls"] for w in ws)/sum(w["bars"] for w in ws)*4,
                         stem_tone_medians={k:st.mean(t["medians"][k] for t in tw) for k in ("f0_hz","h2_db","h3_db")}))
    doc = dict(date="2026-11-01", measured_seconds=528, calibration_tracks=8, validation_tracks=3,
               caveats=["Pitch segments are not ground-truth MIDI gates or attacks; same-pitch repeats may merge and pitch artifacts may split.",
                        "Amplitude attack counts were rejected: Demucs often ducks bass under a kick, then recovers.",
                        "Stem harmonic levels are not dry synth parameters. Strongest FFT peaks and zero-crossing attack estimates were rejected.",
                        "Style tempo is a half/double-time hypothesis, kept separate from analyzer BPM.",
                        "Hat estimates include separation/onset errors and are not hand-labelled ground truth."], tracks=rows)
    save(ROOT / "docs/audit/reference/darktrap_measurements_2026_11.json", doc)
    lines = ["| Референс | BPM¹ | Kick/такт | Сегменты баса/такт² | Длина median, четверти² | Hat/такт | 1/8 | 1/16T | Rolls/4 такта |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        lines.append(f"| {r['id']}: {r['file'].replace('|','')} | {r['style_bpm'][0]:.0f} | {r['kick_starts_per_bar']:.2f} | {r['bass_pitch_segments_per_bar']:.2f} | {r['bass_segment_median_quarters']:.2f} | {r['hats_per_bar']:.2f} | {r['hat_gap_fraction'].get('1/8',0):.2f} | {r['hat_gap_fraction'].get('1/16T',0):.2f} | {r['hat_rolls_per_four_bars']:.2f} |")
    (CACHE / "corpus-table.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))


def variable_length(n):
    data = [n & 127]
    while n > 127:
        n >>= 7
        data.insert(0, (n & 127) | 128)
    return bytes(data)


def write_midi(path, notes, bass_only=False):
    events = [(0, -1, b"\xff\x51\x03" + (400000).to_bytes(3, "big"))]  # 150 BPM, PPQ 960
    for n in notes:
        if bass_only and n["lane"] != "bass":
            continue
        channel = 0 if n["lane"] == "bass" else 9
        start = max(0, int(n["gridTick"]) + int(n["offset"]))
        end = start + max(1, int(n["length"]))
        pitch, velocity = int(n["pitch"]), int(n["velocity"])
        events += [(start, 1, bytes((0x90|channel,pitch,velocity))), (end, 0, bytes((0x80|channel,pitch,0)))]
    data, previous = bytearray(), 0
    for time, order, message in sorted(events):
        data.extend(variable_length(time - previous) + message)
        previous = time
    data.extend(b"\0\xff\x2f\0")
    path.write_bytes(b"MThd" + struct.pack(">IHHH",6,0,1,960) + b"MTrk" + struct.pack(">I",len(data)) + data)


def export(folder, tag, density_block=1, label=None, density=.4):
    target = AUDIT / "listening" / (label or tag)
    target.mkdir(parents=True, exist_ok=True)
    grouped = collections.defaultdict(list)
    with (folder / f"{tag}_notes.csv").open(newline="") as file:
        for n in csv.DictReader(file):
            if n["substyle"] == "1" and int(n["seed"]) in (1,2,3) and abs(float(n["density"]) - density) < .001:
                grouped[int(n["seed"])].append(n)
    for seed, notes in grouped.items():
        write_midi(target / f"seed{seed:03}_drums_and_bass.mid", notes)
        write_midi(target / f"seed{seed:03}_bass.mid", notes, bass_only=True)
    # --render 3 writes 9 rows per density configuration (3 seeds x kick/bass/mix).
    with (folder / f"{tag}_renders.csv").open(newline="") as file:
        for index, row in enumerate(csv.DictReader(file)):
            if index // 9 != density_block:
                continue
            dest = target / f"seed{int(row['seed']):03}_{row['lane']}.wav"
            shutil.copyfile(row["file"], dest)
    print("Listening export:", target)


def compare(before, after):
    bs, ats = read(before / "before_summary.json"), read(after / "after_summary.json")
    output = []
    for b, a in zip(bs["configurations"], ats["configurations"]):
        assert (b["substyle"],b["density"],b["bars"]) == (a["substyle"],a["density"],a["bars"])
        metrics = {}
        for key in ("kicksPerBar","bassNotesPerBar","bassMeanLength","bassGapFill","bassShortRate","hatsPerBar",
                    "hatEighthCoverage","patternsWithRoll","trueRollNotesPerBar","extraSnaresPerBar"):
            scale = .25 if key == "bassMeanLength" else 1
            metrics[key] = dict(before=b["metrics"][key]["mean"]*scale, after=a["metrics"][key]["mean"]*scale)
        output.append(dict(density=a["density"], hard_failure_rate_before=b["hardFailureRate"],
                           hard_failure_rate_after=a["hardFailureRate"], determinism_failures=a["determinismFailures"],
                           near_duplicates_before=b["nearDuplicateRate"], near_duplicates_after=a["nearDuplicateRate"], metrics=metrics))
    save(AUDIT / "comparison.json", output)
    print(json.dumps(output, indent=2))


def proofs():
    def csv_rows(path):
        with path.open(newline="") as f:
            return list(csv.DictReader(f))
    before = csv_rows(CACHE / "baseline/before_patterns.csv")
    after = csv_rows(CACHE / "final-controls/after_patterns.csv")
    assert len(before) == len(after) == 6000
    columns = ["skeletonHash","kickHash","snareHash","hatHash","otherHash","firstTwoHash","firstTwoKickHash"]
    changed = controls = 0
    for b,a in zip(before,after):
        assert (b["substyle"],b["seed"],b["density"],b["bars"]) == (a["substyle"],a["seed"],a["density"],a["bars"])
        if b["substyle"] != "DarkTrap":
            controls += 1
            changed += any(b[k]!=a[k] for k in columns)
    grouped = collections.defaultdict(list)
    roll_lengths = collections.Counter()
    for r in csv_rows(CACHE / "after-final/after_notes.csv"):
        if r["lane"] == "bass":
            grouped[(r["seed"],r["density"])].append(r)
        if "roll" in r["role"]:
            roll_lengths[r["length"]] += 1
    pairs = mismatched = overlaps = 0
    for rows in grouped.values():
        rows.sort(key=lambda r:int(r["gridTick"]))
        for a,b in zip(rows,rows[1:]):
            overlaps += int(a["gridTick"])+int(a["length"])>int(b["gridTick"])
            if "bass_pickup" in a["role"] and int(b["gridTick"])-int(a["gridTick"])<=240:
                pairs += 1
                mismatched += a["pitch"] != b["pitch"]
    tests = {}
    for name in ("core", "lane"):
        text = (CACHE / f"{name}-verified.log").read_text(encoding="utf-8")
        tests[name] = dict(passed=text.count("[PASS]"), failed=text.count("[FAIL]"))
    hashes = {name:hashlib.sha256((CACHE/name).read_bytes()).hexdigest() for name in
              ("HPDG_GenerationQualityLab_before_fixed.exe", "HPDG_GenerationQualityLab_verified.exe")}
    result = dict(non_dark_controls=controls, changed_control_hashes=changed,
                  quick_pickup_pairs=pairs, quick_pickup_pitch_mismatches=mismatched,
                  overlapping_bass_gates=overlaps, product_roll_gate_ticks=dict(roll_lengths),
                  tests=tests, executable_sha256=hashes)
    assert changed == mismatched == overlaps == 0
    assert all(t["failed"]==0 for t in tests.values())
    save(AUDIT / "checks.json", result)
    for label,folder,tag in (("before", "baseline-fixed", "before"), ("after", "after-final", "after"), ("fills", "fills-after", "after")):
        save(AUDIT / f"{label}_summary.json", read(CACHE / folder / f"{tag}_summary.json"))
    performance = {name:read(CACHE / folder / f"{name}_summary.json")["configurations"][0]["timeMs"]
                   for name,folder in (("before","perf-before"),("after","perf-after"))}
    save(AUDIT / "performance.json", performance)
    print(json.dumps(result,indent=2))
    print(json.dumps(performance,indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["corpus","export-before","export-after","export-fills","export-sparse","compare","proofs"])
    args = parser.parse_args()
    if args.action == "corpus":
        corpus()
    elif args.action == "export-before":
        export(CACHE / "baseline-fixed", "before")
    elif args.action == "export-after":
        export(CACHE / "after-final", "after")
    elif args.action == "export-fills":
        export(CACHE / "fills-after", "after", density_block=0, label="fills")
    elif args.action == "export-sparse":
        export(CACHE / "after-final", "after", density_block=0, label="sparse", density=.2)
    elif args.action == "proofs":
        proofs()
    else:
        compare(CACHE / "baseline-fixed", CACHE / "after-final")
