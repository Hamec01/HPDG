"""Verify source/asset provenance, non-Luxury isolation, generator matrix and local audio."""
from pathlib import Path
import csv
import ctypes
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET
import numpy as np
from scipy.io import wavfile
from luxury_reference_audit import ROOT, save, decode


def main():
    out=ROOT/'quality-lab/luxurytrap-2026-11'
    read=lambda name:json.loads((out/name).read_text(encoding='utf-8'))
    inventory=read('inventory.json')
    current_sources={hashlib.sha256(p.read_bytes()).hexdigest() for p in
        Path('C:/Users/Ham_h/Downloads/luxury trap/oneshots').rglob('*.wav')}
    # Some supplied names now contain a replacement character instead of a trademark.
    # Resolve missing paths by content identity; never alter the source files.
    assert all((hashlib.sha256(Path(r['path']).read_bytes()).hexdigest()==r['sha256']
        if Path(r['path']).exists() else r['sha256'] in current_sources) for r in inventory)
    kit=read('kit_manifest.json')
    assert all(hashlib.sha256((ROOT/r['output']).read_bytes()).hexdigest()==r['sha256'] for r in kit)
    before=list(csv.DictReader((out/'rhythm_fix_before_notes.csv').open()))
    after=list(csv.DictReader((out/'rhythm_fix_after_notes.csv').open()))
    a=[r for r in before if r['substyle']!='5']
    b=[r for r in after if r['substyle']!='5']
    assert a==b
    matrix=read('rhythm_fix_matrix_summary.json')
    assert len(matrix['configurations'])==9
    assert all(c['hardFailureRate']==0 and c['determinismFailures']==0 for c in matrix['configurations'])
    core=(ROOT/'Logs/luxury-reviewed-core-final.log').read_text()
    assert '[FAIL]' not in core and core.count('[PASS]')==93
    checks=read('rhythm_review_checks.json')
    assert checks['other_style_rows_unchanged']==len(a)
    for item in ('after','matrix'):
        assert all(checks[item][k]==0 for k in ('missing_first_kick','missing_first_bass','offgrid_bass','overlapping_gates'))
    assert 'PASS: Luxury 18-sound rotation/lock' in (ROOT/'Logs/luxury-sound-check-final.log').read_text()
    # Independent final-event audit across both closed-hat lanes and all density/bar seeds.
    groups={}
    for r in csv.DictReader((out/'rhythm_fix_matrix_notes.csv').open()):
        if r['lane'] not in ('hat','hatfx'):
            continue
        key=(r['bars'],r['seed'],r['density'])
        groups.setdefault(key,[]).append(int(r['gridTick'])+int(r['offset']))
    assert all(len(v)==len(set(v)) for v in groups.values())
    listening=out/'review'; listening.mkdir(exist_ok=True)
    audio=[]
    for row in csv.DictReader((out/'rhythm_review_audio_renders.csv').open()):
        src=Path(row['file']); dst=listening/f"Luxury_seed{row['seed']}_{row['lane']}_138bpm.wav"
        shutil.copyfile(src,dst)
        sr,x=wavfile.read(dst)
        y=x.astype(float)/(np.iinfo(x.dtype).max+1) if np.issubdtype(x.dtype,np.integer) else x
        assert np.all(np.isfinite(y)) and np.max(np.abs(y))>1e-5
        audio.append(dict(file=str(dst),sr=sr,seconds=len(y)/sr,peak=float(np.max(np.abs(y))),sha256=hashlib.sha256(dst.read_bytes()).hexdigest()))
    # Load both newly built VST3 modules and request their actual Steinberg factory.
    modules=[]
    for path in [ROOT/'build/HPDG_artefacts/Release/VST3/HPDG.vst3/Contents/x86_64-win/HPDG.vst3',
                 ROOT/'build/HPDG808_artefacts/Release/VST3/HPDG 808.vst3/Contents/x86_64-win/HPDG 808.vst3']:
        dll=ctypes.WinDLL(str(path))
        factory=dll.GetPluginFactory
        factory.restype=ctypes.c_void_p
        assert factory()
        modules.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),factory_load=True))
    result=dict(original_files_unchanged=len(inventory),kit_assets_verified=len(kit),
        unchanged_non_luxury_note_rows=len(a),matrix_patterns=matrix['seeds']*len(matrix['configurations']),
        matrix_configurations=len(matrix['configurations']),matrix_hard_failures=0,
        current_core_tests_passed=93,bass_rotation_lock_audibility_and_session_restore_check=True,
        closed_hat_duplicate_events=0,audio=audio,modules=modules,
        live_daw_audition=False,initial_failures='Initial hat duplication assertion failed; fixed repair collision. Core link needed follower/composer sources; corrected CMake. Logs retained.')
    save(out/'rhythm_review_validation.json',result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('audio','modules')},indent=2))


if __name__=='__main__':
    main()
