"""Produce the LuxuryTrap research passport and measured plots from cached evidence."""
from pathlib import Path
import argparse
import csv
import json
import sys
import numpy as np
from luxury_reference_audit import ROOT, decode
from luxury_bass_fit import features


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--ffmpeg',required=True)
    ap.add_argument('--bank',type=Path,required=True)
    args=ap.parse_args()
    out=ROOT/'quality-lab/luxurytrap-2026-11'
    data=json.loads((out/'analysis.json').read_text(encoding='utf-8'))
    fit=json.loads((out/'bass_fit.json').read_text(encoding='utf-8'))
    bank=json.loads((args.bank/'manifest.json').read_text(encoding='utf-8'))
    lines=['# LuxuryTrap reconstruction — 2026-11-06','',
        '## Evidence and measurement contract','',
        '- 16 supplied drum/bass pairs; two aligned 24 s windows at 30% and 60% of each stem (64 individual stem windows).',
        '- 69 supplied one-shots: 16 bass, 15 closed hats, 10 clap/rim, 13 snare, 6 kick, 4 open hat, 5 percussion.',
        '- inventory.json stores original paths, byte counts and SHA256. Decoded cuts/raw engine reports are in build/luxury-reference.',
        '- HPDG_BreakLab supplies tempo/hit/pitch hypotheses. Independent 6 kHz spectral-flux detections use thresholds 0.06/0.10/0.16, 2.90 ms hop, minimum 11.61 ms between peaks.',
        '- HF transients are **hat candidates**, including snare/open-hat leakage; these are not annotated ground truth or a recovered MIDI score.',
        '- Half/double tempo chosen nearest 145 BPM is a display hypothesis, not a tempo label. Inconsistent window tempi and confidence <0.5 are flagged below.',
        '- GODS COUNTRY and GODS COUNTRY-1 have identical duration and 0.997 waveform correlation in the first sampled drum window. Do not count them as independent evidence in pooled calibration.',
        '- Bass pitch transcription merges same-pitch attacks: ELEGANT includes a 19.3 s segment (about 54 beats at 167), so its note count/duration cannot calibrate articulation.',
        '- This is descriptive analysis of the supplied corpus, not a held-out genre-recognition experiment. No universal genre accuracy or identical-sound claim is made.','',
        '## Per-reference observations','',
        'Numbers are per-window estimates. Rolls mean >=3 consecutive fast gaps (at least four detected hits).', '',
        '| Reference | BPM hypotheses | HF candidates/bar (threshold .10) | Fast runs/4 bars | Transcribed bass notes/bar | Flags |',
        '|---|---|---|---|---|---|']
    drums=[r for r in data['tracks'] if '_(Drums)' in r['name']]
    bass={r['name'].replace('_(Bass)',''):r for r in data['tracks'] if '_(Bass)' in r['name']}
    for row in drums:
        ws=row['windows']; bs=bass[row['name'].replace('_(Drums)','')]['windows']
        bpms=[w['display_bpm_hypothesis'] for w in ws]
        flags=[]
        if abs(bpms[0]-bpms[1])/min(bpms)>.03: flags.append('tempo/section disagreement')
        if any(w['tempo']['conf']<.5 for w in ws): flags.append('low tempo confidence')
        if row['name'].startswith('6_'):flags.append('near-duplicate rendition')
        if 'ELEGANT' in row['name']:flags.append('bass attack merging')
        fmt=lambda values:' / '.join(f'{v:.2f}' for v in values)
        lines.append('| '+row['name'].replace('_(Drums).mp3','')+' | '+fmt(bpms)+' | '+
            fmt([w['hat_metrics']['0.1']['candidates_per_bar'] for w in ws])+' | '+
            fmt([w['hat_metrics']['0.1']['rolls_per_4bars'] for w in ws])+' | '+
            fmt([w['metrics']['notes_per_bar'] for w in bs])+' | '+', '.join(flags)+' |')
    lines += ['', '## Implemented musical grammar','',
        '- Dedicated Luxury generation replaces the previous shared kick/bass/hat additions, retaining the Trap backbeat and legal kick positions.',
        '- Four seed-stable phrase families: pedal, melodic, rattling, driving. Candidate scoring cannot exchange one family for another. Families and exact probabilities are engineering choices informed by the heterogeneous references, not fitted ground-truth frequencies.',
        '- A/B/A/B′ kick motifs; sparse reinforcement instead of one kick per bass attack. Bass anchors plus independent responses/pickups; final gates are monophonic and ring across the backbeat.',
        '- 8th carrier with softer offbeats; denser families add 16ths. Phrase-local rolls use 32nds/64ths and triplets, minimum four notes, rising/falling/wave/strong-weak contours. Rolls replace their slice of the carrier, with clearance around triplet endpoints.',
        '- Rolls Off really disables rolls. Fills forces phrase-ending ornaments. Half-tempo host mapping uses the existing resolved clock exactly once.',
        '- Half of seeds add a controlled 0/-2/-5/0 semitone roll contour. This is a musical design choice; separated HF material does not establish the source hat pitches.',
        '- Generic repair can no longer layer a closed-hat accent onto an existing Luxury roll. Fine note gates preserve 1/64 articulation instead of turning it into overlapping 16th notes.',
        '- Sample-guided generation retains Luxury closed hats/HatFX and bass onsets. SampleBassFollower changes bass pitches using analysis; styleWindow808Pitch preserves the low register. Exact Copy remains a literal-copy mode, outside this guarantee.',
        '- 63 deduplicated drum lane assets imported from the supplied kit (13 hats also exposed in HatFX), plus 16 C-tuned 808s in the Sub808 sample bank. SHA256 provenance is in kit_manifest.json. Append-only aliases preserve drum indices; saved 808 choices resolve by name when the synth tail moves. Other substyles retain their existing formulas.',
        '- Existing HiHat/HatFX/OpenHat/Perc lanes cover these roles; no new lane/UI or serialization type was necessary.','',
        '## Bass tuning and synthesis','',
        'The neutral sampler trigger MIDI 60 (the owner’s FL C5 naming) is independent of the waveform’s physical fundamental. Physical C1 = MIDI 24 = 32.7032 Hz; C2 = MIDI 36 = 65.4064 Hz. A synth follows physical MIDI pitch; it must not globally transpose by the DAW label difference.',
        '', '16 new copies are tuned to the nearest C octave, preserving the original character and originals. Rate tuning changes duration as in a sampler. MP3 stems are never used as factory one-shots.', '',
        '| Source | Original measured Hz | Copy physical root | Verified cents |', '|---|---:|---|---:|']
    samples={r['name']:r for r in data['samples'] if r['category']=='808'}
    for row in bank:
        name=Path(row['source']).name
        lines.append(f"| {name} | {samples[name]['hz']:.3f} | MIDI {row['physical_root_midi']} / {row['physical_root_hz']:.4f} Hz | {row['measured_cents']:+.2f} |")
    lines += ['', 'Pure synthesis fits use the production BassEngine (not a separate Python sound model). A static harmonic fit seeds nonlinear least-squares refinement of drive, bias, blend, key-tracked tone cutoff, hold and decay. Amplitude is compared after equal-level alignment; no phase/waveform identity is claimed. The attack punch (1.5 st, 12 ms) is a conservative design prior, not a measured recovery of the original transient.', '',
        '| Fit | Harmonics h2–h8 RMSE dB | h2–h6 clipped at −45 dB RMSE | Envelope RMSE dB | Outcome |', '|---|---:|---:|---:|---|']
    for r in fit:
        strong=np.sqrt(np.mean((np.maximum(r['reference_harmonics_db'][:5],-45)-np.maximum(r['synth_harmonics_db'][:5],-45))**2))
        outcome='default pure synth' if 'Spinz' in r['settings']['name'] else 'alternative pure synth' if 'Lex' in r['settings']['name'] else 'rejected: insufficient match'
        lines.append(f"| {r['settings']['name']} | {r['harmonic_rmse_db']:.2f} | {strong:.2f} | {r['envelope_rmse_db']:.2f} | {outcome} |")
    lines += ['', 'Luxury Spinz/Luxury Lex are appended to the embedded HPDG 808 factory presets. Selecting LuxuryTrap initializes Luxury Spinz; unlocked full/bass generation cycles the two synths and sixteen Luxury bank 808s. Lock preserves the sound. No source sample is required by the synth patches. The Lex fit is approximate; the Silk fit was not shipped as a factory preset.', '', 'Audibility correction: the original Snare 10 and Kick 05 were AIFF containers named .wav. Drum imports now decode to real stereo 48 kHz PCM24 WAV, with gain reduction only for peaks over 0.98. Original files remain unchanged; kit_manifest.json records separate source/output hashes.','']
    final=out/'after_summary.json'
    if final.exists():
        before=next(c for c in json.loads((out/'before_summary.json').read_text())['configurations'] if c['substyle']=='LuxuryTrap')
        after=next(c for c in json.loads(final.read_text())['configurations'] if c['substyle']=='LuxuryTrap')
        lines += ['## Production generation verification','',f"Before: 40 seeds. After: {json.loads(final.read_text())['seeds']} seeds. Default 4-bar settings; defaults changed deliberately.", '', '| Metric | Before | After |','|---|---:|---:|']
        for name in ['hatsPerBar','trueRollNotesPerBar','patternsWithRoll','bassNotesPerBar','bassGapFill','kicksPerBar','backbeatCoverage']:
            lines.append(f"| {name} | {before['metrics'][name]['mean']:.3f} | {after['metrics'][name]['mean']:.3f} |")
        lines += ['',f"After hard failure rate: {after['hardFailureRate']}; determinism failures: {after['determinismFailures']}. Additional density/bar/tempo and sample-harmony regression results are recorded in the handoff and validation.json."]
    lines += ['', '## Remaining perceptual validation','',
        'Generated WAVs and fitted reference/synth comparisons are supplied for audition. Automated spectral, rhythmic and finite-output checks do not establish that a person hears an identical source patch or that every generated beat is stylistically convincing. No live FL Studio audition was performed by the agent.','']
    (ROOT/'docs/audit/LUXURY_TRAP_2026_11.md').write_text('\n'.join(lines),encoding='utf-8')
    sys.path.insert(0,str(ROOT/'build/luxury-deps'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(1,2,figsize=(12,4))
    r=next(r for r in fit if 'Spinz' in r['settings']['name'])
    source=next(b for b in bank if Path(b['source']).name==r['source'])
    for path,label in [(source['output'],'Source (C-tuned)'),(r['render'],'Production synth')]:
        x=decode(args.ffmpeg,Path(path)); _,t,env=features(x,22050,source['physical_root_hz'])
        env-=np.max(env[(t>.03)&(t<.2)])
        axes[0].plot(t,env,label=label)
    axes[0].set(xlim=(0,1.2),ylim=(-30,3),xlabel='Seconds',ylabel='RMS dB relative to attack',title='Luxury Spinz: envelope')
    axes[0].legend()
    h=np.arange(2,9)
    axes[1].bar(h-.18,r['reference_harmonics_db'],width=.36,label='Source')
    axes[1].bar(h+.18,r['synth_harmonics_db'],width=.36,label='Synth')
    axes[1].set(xlabel='Harmonic number',ylabel='dB relative to fundamental',title='Stable body: 150–550 ms')
    axes[1].legend(); fig.tight_layout()
    fig.savefig(out/'spinz_fit.png',dpi=150)
    print('Wrote research passport and measured plot')


if __name__=='__main__':
    main()
