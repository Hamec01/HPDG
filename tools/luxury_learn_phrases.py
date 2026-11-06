"""Learn paired four-bar kick/bass phrase cells from full stems with independent pitch support.

Train/validation split is by recording (near-duplicate GODS COUNTRY rendition excluded).
No artist labels or existing genre patterns enter the measurements.
"""
from pathlib import Path
import json,csv,collections,hashlib,math
import numpy as np
from scipy.io import wavfile
from luxury_reference_audit import ROOT,save
from luxury_attack_timing import attack_times,refine_onset
from atmos_passport import parse_hits

OUT=ROOT/'quality-lab/luxurytrap-2026-11'
tracks=json.loads((OUT/'full_phrases_raw.json').read_text(encoding='utf-8'))['tracks']
bass={r['name'].replace('_(Bass)',''):r for r in tracks if '_(Bass)' in r['name']}
cross={r['name']:r for r in json.loads((OUT/'cross_stem_kicks.json').read_text(encoding='utf-8'))['tracks']}
alignment={r['track_id']:r for r in json.loads((OUT/'stem_alignment.json').read_text(encoding='utf-8'))}
all_phrases=[];summaries=[]
for drum in [r for r in tracks if '_(Drums)' in r['name']]:
    name=drum['name'].replace('_(Drums)','');identifier=int(name.split('_')[0])
    if name not in bass:
        summaries.append(dict(name=name,status='drums only; no paired bass; census retained'));continue
    br=bass[name];periods=br['independent_bass']['period_notes']
    timing=alignment[identifier]
    if not timing['reliable']:
        summaries.append(dict(name=name,track_id=identifier,status='bass/mix alignment uncertain; excluded from grammar'))
        continue
    bp=ROOT/'build/luxury-basicpitch/result/notes'/br['sha256'][:12]/'bass.csv'
    candidates=[];differences=[]
    for n in csv.DictReader(bp.open()):
        start,end=float(n['start']),float(n['end']);midi=int(n['midi']);amp=float(n['amplitude'])
        if end-start<.075 or not 20<=midi<=60 or amp<.15:continue
        t=start+min(.15,(end-start)/2)
        supported=[p for p in periods if p['start']<=t<=p['end'] and abs((midi-p['midi']+6)%12-6)<.8]
        if not supported:continue
        p=min(supported,key=lambda p:abs(midi-p['midi']))
        differences.append(round((midi-p['midi'])/12)*12)
        candidates.append(dict(start=start,end=end,midi=midi,amp=amp,period=p['midi']))
    octave_shift=collections.Counter(differences).most_common(1)[0][0] if differences else 0
    sample_rate,wave=wavfile.read(ROOT/'build/luxury-basicpitch'/f"{br['sha256']}.wav")
    waveform_attacks=attack_times(wave,sample_rate)
    def energy(note,midi):
        middle=(note['start']+note['end'])/2;half=min(.10,(note['end']-note['start'])/2)
        a=max(0,round((middle-half)*sample_rate));b=min(len(wave),round((middle+half)*sample_rate))
        y=wave[a:b]*np.hanning(b-a);t=np.arange(b-a)/sample_rate
        return abs(np.sum(y*np.exp(-2j*np.pi*(440*2**((midi-69)/12))*t)))
    notes=[];octave_overrides=0
    for n in sorted(candidates,key=lambda n:(n['start'],-n['amp'])):
        n['midi']-=octave_shift
        if abs(n['midi']-n['period'])>1.2:
            # A loud second harmonic is not evidence of a played octave jump.
            # Use the corroborating periodicity, rather than letting its spectral level
            # turn simultaneous neural harmonics into new melodic notes.
            n['midi']=int(round(n['period']));octave_overrides+=1
        if notes and n['start']-notes[-1]['start']<.045:
            if n['amp']>notes[-1]['amp']:notes[-1]=n
        else:notes.append(n)
    counts=collections.Counter()
    for n in notes:counts[n['midi']]+=min(2,n['end']-n['start'])
    root=counts.most_common(1)[0][0] if counts else 24
    # Separated stems need not share the mix's timing. Keep spectral corroboration on
    # the original waveform above, then remove the measured track delay from note times.
    delay=timing['delay_ms']/1000
    for n in notes:
        n['neural_start']=n['start']
        n['start'],n['attack_supported']=refine_onset(n['start'],waveform_attacks)
        if n['end']-n['start']<.06:n['start']=n['neural_start']
        n['start']-=delay; n['end']-=delay
    notes.sort(key=lambda n:n['start'])
    preferred=[]
    for w in drum['windows']:
        if not w['tempo'] or w['tempo']['conf']<.45:continue
        raw=w['tempo']['bpm'];bpm=min((raw*.5,raw,raw*2),key=lambda v:abs(math.log2(v/145)))
        preferred.append((round(bpm),len([h for h in w['hits'] if h['lane']=='Snare'])))
    weighted=collections.Counter()
    for bpm,weight in preferred:weighted[bpm]+=weight
    target=weighted.most_common(1)[0][0] if weighted else 140
    split='excluded-near-duplicate' if identifier==6 else 'validation' if identifier in (5,9,14) else 'train'
    track_phrases=[];tempos=[];flags=[]
    for wi,w in enumerate(drum['windows']):
        if not w['tempo'] or w['tempo']['conf']<.5:continue
        raw=w['tempo']['bpm'];choices=[raw*k for k in (.5,1,2,2/3,4/3,1.5)]
        bpm=min(choices,key=lambda v:abs(math.log2(v/target)))
        if abs(bpm-target)/target>.10:
            flags.append(dict(window=wi,tempo=raw,reason='section/tempo conflict; census kept, not fitted'));continue
        bar_seconds=240/bpm;tick_seconds=bar_seconds/64
        snares=[h for h in w['hits'] if h['lane']=='Snare' and h['conf']>=.6 and h['vel']>=90]
        if len(snares)<3:continue
        phases=np.array([h['t']%bar_seconds for h in snares])
        hist,_=np.histogram(phases,bins=128,range=(0,bar_seconds),weights=[h['conf']*h['vel']**2 for h in snares])
        smooth=np.roll(hist,1)+hist*2+np.roll(hist,-1);peak=(int(np.argmax(smooth))+.5)*bar_seconds/128
        near=[p for p in phases if abs((p-peak+bar_seconds/2)%bar_seconds-bar_seconds/2)<bar_seconds*.08]
        snare_phase=peak+np.median([(p-peak+bar_seconds/2)%bar_seconds-bar_seconds/2 for p in near])
        origin=(snare_phase-bar_seconds/2)%bar_seconds
        # Fit the clock to repeated snare transients, not a rounded tempo hypothesis.
        # Outlying fill snares do not move the grid. This prevents accumulated drift.
        st=np.array([h['t'] for h in snares])
        beat_index=np.round((st-origin)/bar_seconds-.5)+.5
        mask=np.abs(st-(origin+beat_index*bar_seconds))<.06*bar_seconds
        if np.count_nonzero(mask)>=4 and np.ptp(beat_index[mask])>=2:
            period,phase=np.polyfit(beat_index[mask],st[mask],1)
            if abs(period-bar_seconds)<bar_seconds*.025:
                bar_seconds=float(period);origin=float(phase%bar_seconds)
                tick_seconds=bar_seconds/64;bpm=240/bar_seconds
        mix_report=ROOT/'build/luxury-cross-stem'/f"{timing['identity']}-{wi}-mix.txt"
        mix_hits=parse_hits(mix_report.read_text(encoding='utf-8'))[1] if mix_report.exists() else []
        # Only complete four-bar spans; silence/intro windows remain in the census.
        for first in np.arange(origin,w['seconds']-4*bar_seconds+1e-5,4*bar_seconds):
            begin=w['start']+first;end=begin+4*bar_seconds
            kicks=[];kerr=[]
            for h in w['hits']:
                t=w['start']+h['t']
                if h['lane']!='Kick' or h['conf']<.65 or not begin<=t<end:continue
                exact=(t-begin)/tick_seconds;tick=int(round(exact/4))*4
                if 0<=tick<256 and abs(exact-tick)<=1.25:
                    kicks.append(tick);kerr.append(abs(exact-tick))
            kicks=sorted(set(kicks))
            optional=[]
            xr=cross.get(name)
            xw=next((q for q in xr['windows'] if q['index']==wi),None) if xr else None
            for c in xw['candidates'] if xw else []:
                t=w['start']+c['time']
                if not c['mix_support'] or not begin<=t<end:continue
                exact=(t-begin)/tick_seconds;tick=int(round(exact/4))*4
                if 0<=tick<256 and abs(exact-tick)<=1.25 and tick not in kicks:
                    if c['accepted']:kicks.append(tick)
                    else:optional.append(tick) # kick OR bass transient; optional reinforcement, not annotation
            kicks=sorted(set(kicks));optional=sorted(set(optional))[:24]
            ns=[n for n in notes if begin<=n['start']<end]
            events=[]
            for i,n in enumerate(ns):
                # Neural onset tails are not musical 32nd-note offsets. Reconstruct the
                # straight 16th carrier only after alignment to the original mix.
                exact=(n['start']-begin)/tick_seconds
                tick=int(round(exact/4))*4
                if not 0<=tick<256:continue
                nxt=ns[i+1]['start'] if i+1<len(ns) else end
                gate=int(round((min(n['end'],nxt,end)-n['start'])/tick_seconds))
                degree=n['midi']-root
                if not -12<=degree<=19:continue
                event=dict(tick=tick,degree=degree,gate=max(1,gate),velocity=round(84+min(1,n['amp'])*30),
                    onset_snap_error_ticks=abs(exact-tick))
                if events and events[-1]['tick']==tick:continue
                events.append(event)
            # Fit only corroborated, active groove spans; retain all rejected windows in flags/census.
            if not 2<=len(kicks)<=24 or not 4<=len(events)<=32:continue
            if any(sum(k//64==b for k in kicks)>6 for b in range(4)):continue
            if any(sum(n['tick']//64==b for n in events)>8 for b in range(4)):continue
            # Do not teach a floating / weakly aligned low end to the generator.
            # Genuine counter notes survive; a phrase still needs several joint anchors.
            joint=sum(n['tick'] in kicks for n in events)/len(events)
            # Missing drum-stem kicks must not exclude a genuine bass-led phrase.
            # Full-mix low-end detections corroborate timing, not the instrument label.
            mix_support=[]
            for h in mix_hits:
                if h['lane']!='Kick' or h['conf']<.65:continue
                exact=(w['start']+h['t']-begin)/tick_seconds
                tick=int(round(exact/4))*4
                if 0<=tick<256 and abs(exact-tick)<=1.25:mix_support.append(tick)
            low_end=sum(n['tick'] in kicks or n['tick'] in mix_support for n in events)/len(events)
            # Kick simultaneity is diagnostic, not a requirement: these sources include
            # bass-led sections and kicks missing from the drum stem. Timing still has
            # to fit the independently fitted grid.
            if np.median([n['onset_snap_error_ticks'] for n in events])>1.25:continue
            track_phrases.append(dict(track_id=identifier,source=drum['name'],bass_sha256=br['sha256'],
                drums_sha256=drum['sha256'],split=split,window=wi,start_seconds=begin,bpm=bpm,
                root_midi=root,kicks=kicks,optional_kicks=[],bass=events,mean_kick_snap_error_ticks=float(np.mean(kerr)),
                bass_mix_delay_ms=timing['delay_ms'],joint_anchor_share=joint,mix_low_end_support=low_end))
            tempos.append(bpm)
    # Do not let a single long recording dominate the phrase catalogue.
    unique={hashlib.sha256(json.dumps((p['kicks'],p['bass']),sort_keys=True).encode()).hexdigest():p for p in track_phrases}
    chosen=list(unique.values())
    if len(chosen)>24:chosen=[chosen[i] for i in np.linspace(0,len(chosen)-1,24).round().astype(int)]
    all_phrases+=chosen
    summaries.append(dict(name=name,track_id=identifier,split=split,seconds=br['seconds'],
        supported_basicpitch_notes=len(notes),legacy_line_notes=sum(len(w['notes']) for w in br['windows']),
        independent_envelope_attacks={k:len(v) for k,v in br['independent_bass']['attacks'].items()},
        octave_shift=octave_shift,spectral_octave_overrides=octave_overrides,root_midi=root,reference_bpm=target,phrases=len(chosen),flags=flags,
        bass_mix_delay_ms=timing['delay_ms'],
        same_pitch_share=sum(a['midi']==b['midi'] for a,b in zip(notes,notes[1:]))/max(1,len(notes)-1)))
    print(identifier,split,'BP-supported',len(notes),'legacy',summaries[-1]['legacy_line_notes'],'phrases',len(chosen),flush=True)
save(OUT/'reference_phrase_catalogue_v2.json',dict(tracks=summaries,phrases=all_phrases,
    inference='Mix-aligned bass onsets on a snare-fitted 16th grid; independently supported pitch; no manually annotated score.'))
train=[p for p in all_phrases if p['split']=='train'];assert len(train)>=40,len(train)
header=['#pragma once','#include <array>','namespace bbg {',
    'struct LuxuryReferenceBass { int tick = 0, degree = 0, gate = 1, velocity = 100; };',
    'struct LuxuryReferencePhrase { int track = 0, kickCount = 0, bassCount = 0; std::array<int,24> kicks {}; std::array<LuxuryReferenceBass,32> bass {}; int optionalKickCount = 0; std::array<int,24> optionalKicks {}; };',
    f'inline constexpr std::array<LuxuryReferencePhrase,{len(train)}> kLuxuryReferencePhrases {{{{']
for p in train:
    kicks=','.join(map(str,p['kicks']))
    ns=','.join('{'+','.join(str(n[k]) for k in ('tick','degree','gate','velocity'))+'}' for n in p['bass'])
    optional=','.join(map(str,p['optional_kicks']))
    header.append('{'+f"{p['track_id']},{len(p['kicks'])},{len(p['bass'])},{{{{{kicks}}}}},{{{{{ns}}}}},{len(p['optional_kicks'])},{{{{{optional}}}}}"+'},')
header+=['}};',
    'inline int luxuryReferencePhraseIndex(int seed, int candidate) {',
    '  unsigned s = static_cast<unsigned>(seed) * 2654435761u;',
    '  s ^= s >> 16; s *= 2246822519u; s ^= s >> 13;',
    '  const int track = kLuxuryReferencePhrases[s % kLuxuryReferencePhrases.size()].track;',
    '  std::array<int,kLuxuryReferencePhrases.size()> options {}; int count = 0;',
    '  for (int i=0;i<static_cast<int>(kLuxuryReferencePhrases.size());++i) if(kLuxuryReferencePhrases[i].track==track) options[count++]=i;',
    '  return options[(s/17u+static_cast<unsigned>(candidate))%static_cast<unsigned>(count)];','}','}']
(ROOT/'Source/Engine/Trap/LuxuryReferencePhrases.h').write_text('\n'.join(header)+'\n',encoding='utf-8')
print('Training catalogue',len(train),'phrases; held-out',len([p for p in all_phrases if p['split']=='validation']))
