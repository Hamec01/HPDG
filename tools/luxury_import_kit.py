"""Import local Luxury assets. Drum aliases append; saved 808 choices resolve by name."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import wave
import numpy as np
from luxury_reference_audit import ROOT, save


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--bass-bank',type=Path)
    ap.add_argument('--ffmpeg',required=True)
    args=ap.parse_args()
    rows=[]
    for category,lanes in {'hihat':['HiHat','HatFX'],'kick':['Kick'],
        'clap':['ClapGhost'],'snare':['Snare'],'open hat':['OpenHat'],'perc':['Perc']}.items():
        seen=set()
        for src in sorted((args.source/'oneshots'/category).glob('*.wav')):
            digest=hashlib.sha256(src.read_bytes()).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            for lane in lanes:
                folder=ROOT/'Samples/Trap'/lane
                aliases=folder/'sample-names.json'
                names=json.loads(aliases.read_text(encoding='utf-8')) if aliases.exists() else {}
                label=f'Luxury {lane} {len(seen):02}'
                dst=folder/(label+'.wav')
                alias='zzzz '+label
                old=[names.get(p.name,p.stem) for p in folder.glob('*.wav') if not p.stem.startswith('Luxury ')]
                assert all(n.casefold()<alias.casefold() for n in old), f'Would shift indices in {lane}'
                # Extensions in the supplied kit are not reliable (AIFF named .wav).
                # Emit real PCM WAV, preserve stereo, and contain floating-point overs.
                raw=subprocess.check_output([args.ffmpeg,'-v','error','-nostdin','-i',str(src),
                    '-ar','48000','-ac','2','-f','f32le','-'])
                audio=np.frombuffer(raw,dtype='<f4').copy()
                peak=float(np.max(np.abs(audio)))
                gain=min(1.0,.98/max(peak,1e-9))
                audio*=gain
                # Write PCM directly: the bundled ffmpeg encoder can drop very short
                # raw-pipe inputs during probing. Avoid another decode/encode boundary.
                pcm=np.rint(audio.astype(float)*8388607).astype('<i4')
                payload=pcm.view(np.uint8).reshape(-1,4)[:,:3].tobytes()
                assert len(payload)>0 and len(payload)%6==0
                with wave.open(str(dst),'wb') as writer:
                    writer.setnchannels(2); writer.setsampwidth(3); writer.setframerate(48000)
                    writer.writeframes(payload)
                names[dst.name]=alias
                save(aliases,names)
                rows.append(dict(source=str(src),output=str(dst.relative_to(ROOT)),
                    source_sha256=digest,sha256=hashlib.sha256(dst.read_bytes()).hexdigest(),gain=gain))
    if args.bass_bank:
        bank=json.loads((args.bass_bank/'manifest.json').read_text(encoding='utf-8'))
        folder=ROOT/'Samples/Trap/Sub808'
        aliases=folder/'sample-names.json'
        names=json.loads(aliases.read_text(encoding='utf-8')) if aliases.exists() else {}
        old=[names.get(p.name,p.stem) for p in folder.glob('*.wav') if not p.stem.startswith('Luxury ')]
        for i,row in enumerate(bank,1):
            src=Path(row['output'])
            label=f"Luxury 808 {i:02} - C{row['physical_root_midi']//12-1}"
            alias='zzzz '+label
            assert all(n.casefold()<alias.casefold() for n in old)
            dst=folder/(label+'.wav')
            shutil.copyfile(src,dst)
            names[dst.name]=alias
            rows.append(dict(source=str(src),output=str(dst.relative_to(ROOT)),sha256=row['output_sha256'],
                original_source_sha256=row['source_sha256'],physical_root_midi=row['physical_root_midi']))
        save(aliases,names)
    save(ROOT/'quality-lab/luxurytrap-2026-11/kit_manifest.json',rows)
    print('Imported',len(rows),'lane assets, originals unchanged')


if __name__=='__main__':
    main()
