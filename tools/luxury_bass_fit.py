"""Fit harmonic shape and envelope of local 808s; verify with production BassEngine renders.

Features, not waveform identity: cannot recover an original patch uniquely from a WAV.
"""
from pathlib import Path
import argparse
import json
import subprocess
import numpy as np
from scipy.signal import stft, butter, sosfiltfilt
from scipy.ndimage import uniform_filter1d
from scipy.optimize import least_squares
from luxury_reference_audit import decode, save, pitch_frames

ROOT = Path(__file__).resolve().parents[1]


def features(x, sr, hz):
    a,b = round(sr*.15), min(round(sr*.55),len(x))
    y = x[a:b]*np.hanning(b-a)
    t = np.arange(b-a)/sr
    harmonics = np.array([abs(np.sum(y*np.exp(-2j*np.pi*hz*h*t))) for h in range(1,9)])
    hdb = 20*np.log10(np.maximum(harmonics[1:]/max(harmonics[0],1e-9),1e-5))
    env = np.sqrt(np.maximum(1e-12, uniform_filter1d(x*x, round(sr*.06))))
    times = np.arange(len(x))/sr
    level = 20*np.log10(env+1e-9)
    return hdb, times, level


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank',type=Path,required=True)
    ap.add_argument('--ffmpeg',required=True)
    args=ap.parse_args()
    out=ROOT/'quality-lab/luxurytrap-2026-11'
    scratch=ROOT/'build/luxury-reference/fit'
    scratch.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((args.bank/'manifest.json').read_text(encoding='utf-8'))
    names={'808 Spinz.wav':'Luxury Spinz','Lex 808.wav':'Luxury Lex','bass - seam.wav':'Luxury Silk'}
    result=[]
    phase=np.arange(8192)*2*np.pi/8192
    sine=np.sin(phase)
    for row in manifest:
        original=Path(row['source']).name
        if original not in names:
            continue
        x=decode(args.ffmpeg,Path(row['output']))
        sr=22050; hz=row['physical_root_hz']
        # Use measured stable pitch for harmonic estimates, avoiding tiny tuning leakage.
        measured=np.median([f['hz'] for f in pitch_frames(x,sr)])
        hdb,t,env=features(x,sr,measured)
        def spectrum(p):
            drive,bias,mix,cutoff=p
            y=(1-mix)*sine+mix*(np.tanh(drive*(sine+bias))-np.tanh(drive*bias))/np.tanh(drive)
            spec=np.abs(np.fft.rfft(y))[1:9]
            # BassEngine uses four cascaded biquads, two 4th-order Butterworths.
            frequencies=np.arange(1,9)*65.406391325
            spec/=1+(frequencies/cutoff)**8
            return 20*np.log10(np.maximum(spec[1:]/spec[0],1e-5))
        fit=least_squares(lambda p:np.clip(spectrum(p),-60,0)-np.clip(hdb,-60,0),
            [3,.05,.7,900],bounds=([.2,-.4,.02,200],[12,.4,1,6000]),max_nfev=300)
        peak=float(np.max(env[(t>.03)&(t<.2)]))
        mask=(t>.08)&(t<min(len(x)/sr-.08,2))&(env>peak-20)
        tt,ee=t[mask][::100],env[mask][::100]
        af=least_squares(lambda p:p[0]-6.0206*np.maximum(0,tt-p[1])/p[2]-ee,
            [peak,.05,.4],bounds=([-80,0,.03],[3,.8,5]))
        # Pitch punch estimate uses periodic body after 80 ms; early transient remains a fit limitation.
        settings=dict(name=names[original],midi=row['physical_root_midi'],seconds=2,
            punchSemitones=1.5,punchMs=12,drive=float(fit.x[0]),bias=float(fit.x[1]),
            saturation=float(fit.x[2]),toneHz=float(fit.x[3]),attackMs=1,
            holdSeconds=float(af.x[1]),decaySeconds=float(af.x[2]))
        path=scratch/(settings['name'].replace(' ','_')+'.json')
        wav=path.with_suffix('.wav')
        # Refine on the actual nonlinear production voice: its decaying oscillator enters
        # saturation before the output envelope, so a static waveshaper fit is only a seed.
        def production_residual(p):
            settings.update(drive=float(p[0]),bias=float(p[1]),saturation=float(p[2]),
                toneHz=float(p[3]),holdSeconds=float(p[4]),decaySeconds=float(p[5]))
            save(path,settings)
            subprocess.run([str(ROOT/'build/Release/HPDG_Bass808Lab.exe'),'--render-json',str(path),str(wav)],check=True,stdout=subprocess.DEVNULL)
            yy=decode(args.ffmpeg,wav)
            hh,_,ll=features(yy,sr,hz)
            common=min(len(env),len(ll))
            mask=(t[:common]>.08)&(t[:common]<min(len(x)/sr-.1,1.5))&(env[:common]>peak-20)
            difference=ll[:common][mask][::150]-env[:common][mask][::150]
            difference-=np.median(difference)
            return np.r_[np.clip(hh[:5],-45,0)-np.clip(hdb[:5],-45,0),difference*.5]
        initial=[settings[k] for k in ('drive','bias','saturation','toneHz','holdSeconds','decaySeconds')]
        actual=least_squares(production_residual,initial,bounds=([.2,-.4,.02,200,0,.03],[12,.4,1,6000,.8,5]),
            diff_step=.03,max_nfev=45,ftol=.005,xtol=.005)
        production_residual(actual.x)
        save(path,settings)
        subprocess.run([str(ROOT/'build/Release/HPDG_Bass808Lab.exe'),'--render-json',str(path),str(wav)],check=True)
        synth=decode(args.ffmpeg,wav)
        sh,st,se=features(synth,sr,hz)
        harmonic_error=float(np.sqrt(np.mean((np.maximum(sh,-60)-np.maximum(hdb,-60))**2)))
        common=min(len(env),len(se))
        mask=(t[:common]>.08)&(t[:common]<min(len(x)/sr-.1,1.5))&(env[:common]>peak-20)
        gain=float(np.median(env[:common][mask]-se[:common][mask]))
        envelope_error=float(np.sqrt(np.mean((env[:common][mask]-se[:common][mask]-gain)**2)))
        result.append(dict(source=original,source_sha256=row['source_sha256'],settings=settings,
            reference_harmonics_db=hdb.tolist(),synth_harmonics_db=sh.tolist(),
            harmonic_rmse_db=harmonic_error,envelope_rmse_db=envelope_error,
            normalized_gain_difference_db=gain,render=str(wav)))
        print(settings['name'],settings,'harmonic RMSE',harmonic_error,'envelope RMSE',envelope_error,flush=True)
    save(out/'bass_fit.json',result)


if __name__=='__main__':
    main()
