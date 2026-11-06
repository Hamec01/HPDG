from pathlib import Path
import csv,json
p=Path('quality-lab/luxurytrap-2026-11');read=lambda n:list(csv.DictReader((p/(n+'_notes.csv')).open()))
plain=read('rhythm_review_audio');results=json.loads((p/'sample_pipeline_review_checks.json').read_text());lanes={'kick','bass','hat','hatfx'}
for tag in ('rhythm_review_sample_bass','rhythm_review_sample_mix'):
 rows=read(tag);matched=bars=violations=0
 for seed in ('1','2','3','4'):
  a=[r for r in plain if r['seed']==seed];b=[r for r in rows if r['seed']==seed]
  sig=lambda rs:sorted((r['lane'],r['gridTick'],r['offset']) for r in rs if r['lane'] in lanes)
  matched+=sig(a)==sig(b)
  aa={int(r['gridTick']):int(r['pitch']) for r in a if r['lane']=='bass'};bb={int(r['gridTick']):int(r['pitch']) for r in b if r['lane']=='bass'}
  for bar in range(4):
   delta={bb[t]-v for t,v in aa.items() if t in bb and t//3840==bar}
   if delta:bars+=1;violations+=len(delta)>1
 results[tag].update(same_core_lane_onsets=matched,contour_bars_compared=bars,contour_violations=violations)
 assert matched==4 and violations==0
(p/'sample_pipeline_review_checks.json').write_text(json.dumps(results,indent=2));print('Both sample pipelines: 4/4 core rhythms preserved; all compared bar contours preserved')
