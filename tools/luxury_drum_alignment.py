import json
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter,sosfiltfilt,resample_poly,correlate,correlation_lags
from scipy.ndimage import uniform_filter1d
p=Path('quality-lab/luxurytrap-2026-11'); rows=json.loads((p/'stem_alignment.json').read_text())
for r in rows:
 dr=next(q for q in json.loads((p/'full_phrases_raw.json').read_text())['tracks'] if q['name'].startswith(str(r['track_id'])+'_') and '_(Drums)' in q['name']);scores=[]
 for wi in range(1,6):
  mpath=Path('build/luxury-cross-stem')/f"{r['identity']}-{wi}-mix.wav";dpath=Path('build/luxury-full-phrases')/f"{dr['sha256']}-{wi}.wav"
  if not mpath.exists() or not dpath.exists():continue
  sr,m=wavfile.read(mpath);sr,d=wavfile.read(dpath);n=min(len(m),len(d));m=m[:n].astype(float);d=d[:n].astype(float)
  f=butter(3,[500,7000],btype='bandpass',fs=sr,output='sos');d=sosfiltfilt(f,d);m=sosfiltfilt(f,m)
  d=np.diff(np.sqrt(np.maximum(0,uniform_filter1d(d*d,round(sr*.015))))[::110]);m=np.diff(np.sqrt(np.maximum(0,uniform_filter1d(m*m,round(sr*.015))))[::110]);d-=np.mean(d);m-=np.mean(m)
  c=correlate(d,m,method='fft');lags=correlation_lags(len(d),len(m));ok=abs(lags)<60;i=np.argmax(c[ok]);scores.append(dict(window=wi,delay_ms=round(int(lags[ok][i])*110/sr*1000,1),correlation=float(c[ok][i]/max(1e-9,np.linalg.norm(d)*np.linalg.norm(m)))))
 reliable=[s['delay_ms'] for s in scores if s['correlation']>.3];median=float(np.median(reliable)) if reliable else 0
 r['drums_delay_ms']=median;r['drums_alignment_windows']=scores;r['relative_bass_delay_ms']=r['delay_ms']-median
 print(r['track_id'],'bass',r['delay_ms'],'drums',median,'relative',r['relative_bass_delay_ms'],flush=True)
(p/'stem_alignment.json').write_text(json.dumps(rows,indent=2))
