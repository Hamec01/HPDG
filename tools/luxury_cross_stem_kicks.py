"""Cross-check kick evidence in drum/bass/recombined stems and the original mix.

Additional bass-stem kick candidates need independent mix detection and a template margin
against the supplied 808s. Ambiguous candidates are recorded, never silently treated as pauses.
"""
from pathlib import Path
import argparse,json,hashlib,subprocess,concurrent.futures
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter,sosfiltfilt,resample_poly
from luxury_reference_audit import ROOT,decode,save
from atmos_passport import parse_hits

ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True)
ap.add_argument('--ffmpeg',required=True);args=ap.parse_args()
OUT=ROOT/'quality-lab/luxurytrap-2026-11';cache=ROOT/'build/luxury-cross-stem';cache.mkdir(exist_ok=True)
tracks=json.loads((OUT/'full_phrases_raw.json').read_text(encoding='utf-8'))['tracks']
basses={r['name'].replace('_(Bass)',''):r for r in tracks if '_(Bass)' in r['name']}
SR=8000

def features(audio,onset):
    bands=[]
    for lo,hi in [(25,60),(60,120),(120,240),(240,480),(480,960),(960,1900)]:
        a=max(0,round((onset-.1)*SR));b=min(len(audio),round((onset+.23)*SR))
        z=audio[a:b]
        if len(z)<40:return None
        y=sosfiltfilt(butter(2,[lo,hi],btype='bandpass',fs=SR,output='sos'),z)
        t=np.arange(len(y))/SR+a/SR-onset
        baseline=np.mean(y[(t>=-.08)&(t<-.02)]**2) if np.any((t>=-.08)&(t<-.02)) else 0
        for begin,end in [(0,.025),(.025,.06),(.06,.12),(.12,.21)]:
            part=y[(t>=begin)&(t<end)]
            bands.append(max(1e-12,float(np.mean(part**2))-baseline) if len(part) else 1e-12)
    v=10*np.log10(np.array(bands));v-=max(v)
    return np.maximum(v,-55)

template=[];labels=[]
for category in ('kick','808'):
    for p in sorted((args.source/'oneshots'/category).glob('*.wav')):
        x=decode(args.ffmpeg,p,sr=SR)
        for semitones in (-4,0,4):
            # Template augmentation changes rate, not labels; no generator audio enters this check.
            rate=2**(semitones/12);y=np.interp(np.arange(0,len(x),rate),np.arange(len(x)),x)
            y=np.r_[np.zeros(800),y,np.zeros(2000)]
            f=features(y,.1)
            if f is not None:template.append(f);labels.append(category)
template=np.array(template);labels=np.array(labels)
scale=np.maximum(5,np.std(template,axis=0));template/=scale

def margin(audio,t):
    f=features(audio,t)
    if f is None:return None
    d=np.sqrt(np.mean((template-f/scale)**2,axis=1))
    k=float(np.min(d[labels=='kick']));b=float(np.min(d[labels=='808']))
    return dict(kick_distance=k,bass_distance=b,kick_margin=b-k,confident_kick=k<b*.80)

def analyze(path,raw,bpm):
    if not raw.exists():
        result=subprocess.run([str(ROOT/'build/Release/HPDG_BreakLab.exe'),'analyze',str(path),
            '--bpm',str(bpm),'--cut','--hits'],capture_output=True,check=True,stdin=subprocess.DEVNULL)
        raw.write_text(result.stdout.decode('utf-8',errors='replace'),encoding='utf-8')
    return parse_hits(raw.read_text(encoding='utf-8'))[1]

rows=[]
for drum in [r for r in tracks if '_(Drums)' in r['name']]:
    key=drum['name'].replace('_(Drums)','')
    if key not in basses:continue
    br=basses[key]
    original=args.source/(key.split('_',1)[1])
    identity=hashlib.sha256((drum['sha256']+br['sha256']+(hashlib.sha256(original.read_bytes()).hexdigest() if original.exists() else 'no-mix')).encode()).hexdigest()
    resultfile=cache/f'{identity}-v1.json'
    if resultfile.exists():
        rows.append(json.loads(resultfile.read_text(encoding='utf-8')));print(key,'cached',flush=True);continue
    dx=decode(args.ffmpeg,args.source/'StEMS'/drum['name']);bx=decode(args.ffmpeg,args.source/'StEMS'/br['name'])
    mx=decode(args.ffmpeg,original) if original.exists() else None
    low=resample_poly(bx,160,441)
    row=dict(name=key,drum_sha256=drum['sha256'],bass_sha256=br['sha256'],
        original_mix=str(original) if original.exists() else None,windows=[])
    for wi,w in enumerate(drum['windows']):
        if not w['tempo']:continue
        a=round(w['start']*22050);b=round((w['start']+w['seconds'])*22050)
        n=min(len(dx[a:b]),len(bx[a:b]));combined=.5*(dx[a:a+n]+bx[a:a+n])
        stemwav=cache/f'{identity}-{wi}-sum.wav';mixwav=cache/f'{identity}-{wi}-mix.wav'
        if not stemwav.exists():wavfile.write(stemwav,22050,combined)
        jobs=[(stemwav,cache/f'{identity}-{wi}-sum.txt')]
        if mx is not None:
            if not mixwav.exists():wavfile.write(mixwav,22050,mx[a:b])
            jobs.append((mixwav,cache/f'{identity}-{wi}-mix.txt'))
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            hits=list(pool.map(lambda pair:analyze(*pair,w['tempo']['bpm']),jobs))
        original_kicks=[h['t'] for h in w['hits'] if h['lane']=='Kick' and h['conf']>=.65]
        mix_kicks=[h['t'] for h in hits[-1] if h['lane']=='Kick' and h['conf']>=.65] if mx is not None else []
        candidates=[]
        for h in hits[0]:
            if h['lane']!='Kick' or h['conf']<.65 or any(abs(h['t']-t)<.07 for t in original_kicks):continue
            ev=margin(low,w['start']+h['t'])
            corroborated=mx is not None and any(abs(h['t']-t)<.065 for t in mix_kicks)
            candidates.append(dict(time=h['t'],confidence=h['conf'],mix_support=corroborated,template=ev,
                accepted=bool(corroborated and ev and ev['confident_kick'])))
        row['windows'].append(dict(index=wi,start=w['start'],candidates=candidates,
            drum_kicks=len(original_kicks),sum_kicks=sum(h['lane']=='Kick' and h['conf']>=.65 for h in hits[0]),
            original_mix_kicks=len(mix_kicks)))
    save(resultfile,row);rows.append(row)
    save(OUT/'cross_stem_kicks_progress.json',rows)
    print(key,'added',sum(c['accepted'] for w in row['windows'] for c in w['candidates']),
        'ambiguous',sum(not c['accepted'] for w in row['windows'] for c in w['candidates']),flush=True)
save(OUT/'cross_stem_kicks.json',dict(tracks=rows,
    limitation='Template/mix agreement is corroboration, not manual kick annotation. Ambiguous bass-stem attacks remain unassigned.'))
