"""Second, independent full-stem transcription using the native Basic Pitch port."""
from pathlib import Path
import argparse
import hashlib
import subprocess
from scipy.io import wavfile
from luxury_reference_audit import ROOT, decode, save

ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True)
ap.add_argument('--ffmpeg',required=True);args=ap.parse_args()
cache=ROOT/'build/luxury-basicpitch';cache.mkdir(exist_ok=True)
rows=[];tsv=[]
for path in sorted((args.source/'StEMS').glob('*_(Bass).mp3')):
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    wav=cache/(digest+'.wav')
    if not wav.exists():wavfile.write(wav,22050,decode(args.ffmpeg,path))
    rows.append(dict(name=path.name,sha256=digest,path=str(path),wave=str(wav)))
    tsv.append(f'{digest[:12]}\tbass\t{wav}')
save(cache/'manifest.json',rows)
(cache/'files.tsv').write_text('\n'.join(tsv)+'\n',encoding='utf-8')
subprocess.run([str(ROOT/'build/Release/HPDG_BasicPitchLab.exe'),'corpus',str(cache/'files.tsv'),str(cache/'result'),'2'],check=True)
