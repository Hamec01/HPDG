# Reference data (derived, no audio)

The audio corpora stay outside git: the repository is public and the material is third-party
(sample packs, classic breaks, song stems). What is here lets every benchmark be repeated and
checked without redistributing the audio.

## Contents

| Path | What |
|---|---|
| `boombap_loops_transcribed/*.txt` | `HPDG_BreakLab analyze <loop> --bpm <label> --hits` reports of the 159 boom bap loops (per-hit lane / grid / offset / velocity / confidence). `tools/reference_kicks.py` reads these directly: the kick / hat reference comparison needs **no audio**. Transcribed with build `43e0dc9`+. |
| `tempo_bench/typed_origin_*.txt` | typed-tempo beat-1 check of the 243 labeled loops before / after `43e0dc9` (`origin ms | labeled bpm | file`) |
| `manifest_*.csv` | every corpus file with bytes, SHA-256 and the tempo parsed from its name |
| `user_songs_tbpm.txt` | 616 whole songs from the maintainer's mp3 library (`D:/Downloads`: mp3, Morricone, Theodor Bastard) with a tempo in the ID3 TBPM tag: `path TAB bpm TAB seconds TAB genre`. Tempo benchmark corpus `user_songs` (bpm regex `list`); the tags come from DJ / tagger software and are not verified by ear |
| `techno_fast_tops.tsv` | 16th positions (HiHat lane) of 40 faster techno top / ride loops (PML x Weska Peak Time Tops 128, PML Overdrive Top Loops 135-138), from `tools/loop_bars.py`; compare with the lab column `topBars` |
| `techno_fast_hats.tsv` | the same for hat / ride loops: PML Overdrive Hat / Ride Loops (132-140) and Audentity Dark Techno 2 Hi-Hat loops (125) |
| `techno_dasha_full.tsv` | every onset of 7 Dasha Rush "Techno Toys" full drum loops (131-140, TR-626); compare with `drumBars` |

## Corpora (local paths on the development PC)

| Manifest | Source | Used for |
|---|---|---|
| `manifest_boombap_custom_loops.csv` | `D:/Drums/Boombap/2 GB OF FREE SAMPLES/02_CUSTOM_DRUM_LOOPS_(150+Custom_Drum_Loopz)` (159 loops, tempo in the name) | tempo accuracy, typed-tempo origin, Boom Bap kick / hat reference profiles (70 / 30 split by MD5 of the file name) |
| `manifest_classic_breaks.csv` | `D:/Drums/Boombap/2 GB OF FREE SAMPLES/04_CLASSIC_DRUM_BREAKS_(80+Drm_Loopz)` (84 breaks, tempo in the name) | tempo accuracy (untrimmed breaks) |
| `manifest_stems_sets.csv` | zips in `D:/Downloads/mp3`, unzipped as `s1 … s10`: s1 `Pattern 5 (consolidated) (Cover) Stems.zip`, s2 `Pattern 7 (consolidated) (Cover) Stems.zip`, s3 `sample jasczek Stems (110BPM).zip`, s4 `уста твои устали 110 - 2025-Nov-19_2 (Cover) Stems (110BPM).zip`, s5 `Pattern 1_39 (Remix) (Remastered) Stems.zip`, s6 `Ева (Cover) Stems.zip`, s7 `untitled - обычн (Cover) Stems.zip`, s8 `И пока ты ждал ответ от неё (Cover) Stems.zip`, s9 `Stems.zip`, s10 `Stems (3).zip` | bass-line accuracy (`HPDG_BreakLab lines <set>`; sets with a Bass stem: s2 s3 s5 s6 s8 s9 s10) |
| `manifest_stems07.csv` | `D:/Downloads/mp3/07.безметежнные дни (Cover) Stems.zip` | bass-line accuracy |
| `manifest_stems_40s.csv` | `D:/Downloads/mp3/40.4s Recording (Jul 14 @ 4_03 PM) (Cover) Stems` | bass-line accuracy |

## Portable copy

Everything the benchmarks use is collected in one folder on the development PC (copy it to the
other machine as a whole):

```
D:/HPDG_corpus/
  boombap_loops/02_CUSTOM_DRUM_LOOPS_(150+Custom_Drum_Loopz)/   159 loops
  boombap_loops/04_CLASSIC_DRUM_BREAKS_(80+Drm_Loopz)/          84 breaks
  stems/*.zip                                                   11 stem zips (s1 … s10 + 07; unzip each into its own folder)
  stems/40.4s Recording (Jul 14 @ 4_03 PM) (Cover) Stems/       already unzipped
```

## Checking a corpus on another machine

```
python tools/corpus_manifest.py verify "<corpus folder>" docs/audit/reference/manifest_boombap_custom_loops.csv
```

Exit code 0 = same files, same bytes. Re-run the benchmarks only on a verified corpus (RULE 4).

## Repeating the benchmarks

- Kick / hat profiles (no audio needed):
  `python tools/reference_kicks.py docs/audit/reference/boombap_loops_transcribed [--lane hat] --generated <lab>_patterns.csv`
- Tempo (audio needed): `HPDG_BreakLab analyze <folder> --out <dir>` and `python tools/tempo_bench_compare.py`
  (expects `old1/old2/new1/new2` report folders next to it); typed tempo: `bash tools/tempo_bench_typed.sh`.
- Bass lines (audio needed): `HPDG_BreakLab lines <stems set>`.
