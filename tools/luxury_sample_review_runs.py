from pathlib import Path
import subprocess,json,csv
root=Path('C:/DRUMENGINE');out=root/'quality-lab/luxurytrap-2026-11';lab=root/'build/Release/HPDG_GenerationQualityLab.exe'
a=next(q for q in json.loads((out/'stem_alignment.json').read_text()) if q['track_id']==12)
inputs=[('bass',root/'build/luxury-full-phrases'/f"{a['bass_sha256']}-1.wav"),('mix',root/'build/luxury-cross-stem'/f"{a['identity']}-1-mix.wav")]
for label,path in inputs:
 assert path.is_file(),path
 tag='rhythm_review_sample_'+label
 with (root/'Logs'/f'luxury-{tag}.log').open('w') as log:
  subprocess.run([str(lab),'--genre','trap','--substyle','LuxuryTrap','--seeds','4','--bars','4','--bpm','131','--notes','--render','1','--sample',str(path),'--out',str(out),'--tag',tag],stdout=log,stderr=subprocess.STDOUT,check=True,cwd=root)
 print(tag,'completed',flush=True)
(out/'review_sample_inputs.json').write_text(json.dumps(dict(track=12,bass_source_sha256=a['bass_sha256'],seconds=[24,48],inputs={k:str(v) for k,v in inputs}),indent=2))
