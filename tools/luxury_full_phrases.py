"""Full-duration paired-stem audit, independent bass attacks/pitch, phrase-level evidence.

Does not call a genre generator or infer style from artist names. Caches raw analyzer outputs
by source SHA256 and segment identity. Original media remains read-only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import collections
import math
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, resample_poly, correlate, find_peaks
from scipy.ndimage import uniform_filter1d, median_filter
import subprocess
from luxury_reference_audit import ROOT, decode, save
from atmos_passport import parse_hits, parse_bass

VERSION=2
SR=22050


def bass_evidence(x):
    """Independent monophonic period tracking + attack sensitivity; no key/genre prior."""
    rate=4000
    y=resample_poly(x,80,441)
    y=sosfiltfilt(butter(4,[25,700],btype='bandpass',fs=rate,output='sos'),y)
    size,hop=800,80
    rms=np.sqrt(np.maximum(1e-14,uniform_filter1d(y*y,160)))[::hop]
    db=20*np.log10(rms+1e-9)
    prior=np.min(np.stack([np.roll(db,k) for k in range(2,7)]),axis=0)
    rise=db-prior;rise[:7]=0
    rise[db<np.max(db)-32]=0
    attacks={}
    for threshold in (3,6,9):
        peaks,_=find_peaks(rise,height=threshold,prominence=1.5,distance=4)
        attacks[str(threshold)]=(peaks*.02).tolist()
    pitches=[]
    for a in range(0,len(y)-size,hop):
        z=y[a:a+size].astype(float); level=np.sqrt(np.mean(z*z))
        if level<max(float(np.max(rms))*.025,1e-6):
            pitches.append(None);continue
        z-=z.mean()
        ac=correlate(z,z,mode='full',method='fft')[size-1:]/np.arange(size,0,-1)
        lo,hi=int(rate/180),int(rate/27)
        peaks,_=find_peaks(ac[lo:hi]);peaks+=lo
        if not len(peaks) or ac[0]<=0:
            pitches.append(None);continue
        best=max(ac[peaks])
        if best <= 0:
            pitches.append(None);continue
        eligible=peaks[ac[peaks]>=best*.96]
        p=int(eligible[0]);q=float(ac[p]/ac[0])
        if q<.72:
            pitches.append(None);continue
        delta=.5*(ac[p-1]-ac[p+1])/(ac[p-1]-2*ac[p]+ac[p+1])
        midi=float(69+12*np.log2((rate/(p+delta))/440))
        pitches.append(dict(t=(a+size/2)/rate,midi=midi,confidence=min(1,q)))
    # Split stable pitch regions and independent same-pitch reattacks, not only pitch changes.
    attacks6=attacks['6'];notes=[];current=[]
    def finish():
        if len(current)>=3:
            notes.append(dict(start=current[0]['t']-.10,end=current[-1]['t']+.10,
                midi=float(np.median([p['midi'] for p in current])),
                confidence=float(np.median([p['confidence'] for p in current]))))
    for p in pitches:
        if p is None:
            finish();current=[];continue
        if current:
            old=current[-1]
            reattack=any(old['t']<t<=p['t'] for t in attacks6)
            change=abs(p['midi']-np.median([q['midi'] for q in current[-5:]]))>.8
            if reattack or change:
                finish();current=[]
        current.append(p)
    finish()
    return dict(attacks=attacks,period_notes=notes,
        voiced_frame_share=sum(p is not None for p in pitches)/max(1,len(pitches)))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--ffmpeg',required=True);args=ap.parse_args()
    cache=ROOT/'build/luxury-full-phrases';cache.mkdir(parents=True,exist_ok=True)
    lab=ROOT/'build/Release/HPDG_BreakLab.exe'
    rows=[]
    for path in sorted((args.source/'StEMS').glob('*.mp3')):
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        identity=dict(name=path.name,sha256=digest,bytes=path.stat().st_size)
        final=cache/f'{digest}-v{VERSION}.json'
        if final.exists():
            row=json.loads(final.read_text(encoding='utf-8'));row['name']=path.name
            rows.append(row);print(path.name,'cached',flush=True);continue
        x=decode(args.ffmpeg,path);duration=len(x)/SR
        row=dict(**identity,seconds=duration,windows=[])
        drum='_(Drums)' in path.name
        if not drum:
            row['independent_bass']=bass_evidence(x)
        # Consecutive coverage including intros, silence, switches and endings, no cherry-picking.
        for wi,start in enumerate(range(0,math.ceil(duration),24)):
            cut=x[start*SR:min(len(x),(start+24)*SR)]
            if len(cut)<SR:continue
            raw=cache/f'{digest}-{wi}-v{VERSION}.txt'
            wav=cache/f'{digest}-{wi}.wav'
            if not raw.exists():
                wavfile.write(wav,SR,cut)
                cmd=['analyze',str(wav),'--cut','--hits'] if drum else ['lines',str(wav),'--print']
                result=subprocess.run([str(lab),*cmd],capture_output=True,check=True,stdin=subprocess.DEVNULL)
                raw.write_text(result.stdout.decode('utf-8',errors='replace'),encoding='utf-8')
            text=raw.read_text(encoding='utf-8')
            window=dict(start=start,seconds=len(cut)/SR)
            if drum:
                head,hits=parse_hits(text)
                window.update(tempo=head,hits=hits)
            else:
                notes,key=parse_bass(text);window.update(notes=notes,key=key)
            row['windows'].append(window)
        save(final,row);rows.append(row)
        print(path.name,'full coverage',round(duration,1),'seconds',flush=True)
        save(ROOT/'quality-lab/luxurytrap-2026-11/full_phrases_progress.json',rows)
    save(ROOT/'quality-lab/luxurytrap-2026-11/full_phrases_raw.json',dict(version=VERSION,tracks=rows,
        limitations=['Tempo and hits remain analyzer estimates; independent period notes are not a hand-annotated score.',
        'Independent envelope attacks use 3/6/9 dB thresholds; sustained same-pitch attacks can still be missed.',
        'Period tracking smooths over 200 ms and can miss short slides/pickups. Use alongside raw line transcription.']))
    print('FULL CORPUS COMPLETE',len(rows),'stems',flush=True)


if __name__=='__main__':main()
