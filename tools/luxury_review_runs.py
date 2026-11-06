from pathlib import Path
import subprocess,json
root=Path('C:/DRUMENGINE');out=root/'quality-lab/luxurytrap-2026-11';lab=root/'build/Release/HPDG_GenerationQualityLab.exe'
runs=[('rhythm_fix_after',['--genre','trap','--seeds','12','--bars','4','--notes']),('rhythm_fix_matrix',['--genre','trap','--substyle','LuxuryTrap','--seeds','20','--density','matrix','--bars','matrix','--bpm','138','--notes']),('rhythm_fix_70',['--genre','trap','--substyle','LuxuryTrap','--seeds','8','--bpm','70','--notes']),('rhythm_fix_166',['--genre','trap','--substyle','LuxuryTrap','--seeds','8','--bpm','166','--notes']),('rhythm_review_audio',['--genre','trap','--substyle','LuxuryTrap','--seeds','4','--bpm','138','--key','0','--notes','--render','4'])]
for tag,options in runs:
 with (root/'Logs'/f'luxury-{tag}.log').open('w') as log:subprocess.run([str(lab),*options,'--out',str(out),'--tag',tag],stdout=log,stderr=subprocess.STDOUT,check=True,cwd=root)
 print(tag,'completed',flush=True)
