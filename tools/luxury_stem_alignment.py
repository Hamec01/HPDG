import json,hashlib
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter,sosfiltfilt,resample_poly,correlate,correlation_lags
from scipy.ndimage import uniform_filter1d
p=Path('quality-lab/luxurytrap-2026-11');raw=json.loads((p/'full_phrases_raw.json').read_text())['tracks'];rows=[]
for r in json.loads((p/'cross_stem_kicks.json').read_text())['tracks']:
 ident=int(r['name'].split('_')[0]);br=next(q for q in raw if q['sha256']==r['bass_sha256']);identity=hashlib.sha256((r['drum_sha256']+r['bass_sha256']+hashlib.sha256(Path(r['original_mix']).read_bytes()).hexdigest()).encode()).hexdigest();sr,b=wavfile.read(Path('build/luxury-basicpitch')/f"{br['sha256']}.wav");b=b.astype(float);scores=[]
 for wi in range(1,9):
  path=Path('build/luxury-cross-stem')/f'{identity}-{wi}-mix.wav'
  if not path.exists():continue
  mr,m=wavfile.read(path);bb=b[round(wi*24*sr):round((wi+1)*24*sr)];n=min(len(m),len(bb));m=m[:n].astype(float);bb=bb[:n]
  if n<sr*10:continue
  bb=resample_poly(bb,80,441);m=resample_poly(m,80,441);f=butter(3,[30,180],btype='bandpass',fs=4000,output='sos');bb=sosfiltfilt(f,bb);m=sosfiltfilt(f,m)
  bb=np.diff(np.sqrt(np.maximum(0,uniform_filter1d(bb*bb,120)))[::20]);m=np.diff(np.sqrt(np.maximum(0,uniform_filter1d(m*m,120)))[::20]);bb-=np.mean(bb);m-=np.mean(m)
  c=correlate(bb,m,method='fft');lags=correlation_lags(len(bb),len(m));ok=abs(lags)<60;i=np.argmax(c[ok]);scores.append(dict(window=wi,delay_ms=int(lags[ok][i])*5,correlation=float(c[ok][i]/max(1e-9,np.linalg.norm(bb)*np.linalg.norm(m)))))
 reliable=[s for s in scores if s['correlation']>.28]; delays=[s['delay_ms'] for s in reliable]; med=float(np.median(delays)) if delays else 0;mad=float(np.median(abs(np.array(delays)-med))) if delays else 999
 row=dict(track_id=ident,identity=identity,bass_sha256=br['sha256'],windows=scores,delay_ms=med,delay_mad_ms=mad,reliable=len(delays)>=3 and mad<=15)
 rows.append(row);print(ident,round(med),round(mad),row['reliable'],flush=True)
(p/'stem_alignment.json').write_text(json.dumps(rows,indent=2))
