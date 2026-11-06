"""Compare frozen product-path note dumps; other substyles must be byte-identical."""
from pathlib import Path
import argparse, csv, json, collections

ap=argparse.ArgumentParser()
ap.add_argument('--before',type=Path,required=True)
ap.add_argument('--after',type=Path,required=True)
ap.add_argument('--matrix',type=Path)
ap.add_argument('--out',type=Path,required=True)
args=ap.parse_args()
read=lambda p:list(csv.DictReader(p.open(encoding='utf-8')))
before,after=read(args.before),read(args.after)
others_before=[r for r in before if r['substyle']!='5']
others_after=[r for r in after if r['substyle']!='5']
assert others_before and others_before==others_after,'An unrelated Trap substyle changed'

def measure(rows):
    groups=collections.defaultdict(list)
    for r in rows:
        if r['substyle']=='5':groups[tuple(r[k] for k in ('bars','seed','bpm','density'))].append(r)
    count=notes=joint=late=overlap=missing_kick=missing_bass=offgrid=0
    examples=[]
    for key,rs in groups.items():
        count+=1
        kick={int(r['gridTick']) for r in rs if r['lane']=='kick'}
        bass=sorted((r for r in rs if r['lane']=='bass'),key=lambda r:int(r['gridTick'])+int(r['offset']))
        missing_kick+=0 not in kick
        missing_bass+=not bass or int(bass[0]['gridTick'])+int(bass[0]['offset'])!=0
        for i,n in enumerate(bass):
            t=int(n['gridTick']);notes+=1;joint+=t in kick;offgrid+=t%120!=0
            if kick:
                delta=min((abs(t-k),t-k) for k in kick)[1]
                late+=0<delta<=240
            if i+1<len(bass):
                end=t+int(n['offset'])+int(n['length'])
                nxt=int(bass[i+1]['gridTick'])+int(bass[i+1]['offset'])
                if end>nxt:
                    overlap+=1
                    if len(examples)<5:examples.append(dict(configuration=key,onset=t,end=end,next=nxt))
    return dict(patterns=count,bass_notes=notes,joint_kick_attack_share=joint/max(1,notes),
        late_within_sixteenth_share=late/max(1,notes),missing_first_kick=missing_kick,
        missing_first_bass=missing_bass,offgrid_bass=offgrid,overlapping_gates=overlap,overlap_examples=examples)

result=dict(other_style_rows_unchanged=len(others_before),before=measure(before),after=measure(after))
if args.matrix:result['matrix']=measure(read(args.matrix))
args.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
for k in ('after','matrix'):
    if k in result:
        for invariant in ('missing_first_kick','missing_first_bass','offgrid_bass','overlapping_gates'):
            assert result[k][invariant]==0,(k,invariant,result[k][invariant])
