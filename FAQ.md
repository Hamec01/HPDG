# HPDG — FAQ and getting started

**Version: 0.1.0 Beta 5. Developer: HamloProd. Windows x64, VST3.**

## How do I install it?

Close your DAW. Run HPDG_VST3_Setup_0.1.0-beta.5.exe and allow it to install as administrator. The installer contains the plugin, the whole factory library and the Visual C++ Runtime; there is nothing else to download.

Default folder: C:\Program Files\Common Files\VST3\HPDG.vst3.
Samples: Contents\x86_64-win\Samples inside that bundle.
Guide: **Start → HamloProd → HPDG FAQ**, **HPDG → Instruction** in the plugin header, or Documentation\FAQ.html inside the bundle.

After installing, open your 64-bit DAW and rescan VST3 plugins. Add **HPDG** to an instrument track. You need Windows 10/11 x64 and a DAW with VST3 support. Standalone is not part of this beta.

## Do I need an account, internet or a key?

Not to make music: HPDG works offline, with no account and no activation. With internet, the plugin asks hamloprod.org once per DAW session (and whenever you press **Update**) whether a new version is out. Your license key is sent only when you download an update; nothing about you or your projects is sent. Without internet everything works as usual and the button shows **Offline**.

## How do I get my first pattern?

1. Choose a genre and substyle: Boom Bap, Trap, DnB, Techno or Atmospheric.
2. Set the BPM and the length in bars (Bars).
3. Press **Generate Pattern**.
4. Press **Play** to listen. **Start play with DAW** ties playback to the host transport; **Sync** follows the host tempo.
5. Shape the instruments in the **Instrument Rack**, then export MIDI or WAV.

The library also contains Rap and Drill material for future work; a folder being there does not mean that mode is available in this beta.

## Generate Pattern, Mutate and RG — what is the difference?

**Generate Pattern** creates a pattern from the current settings. **Mutate** varies the current one. **RG** on an instrument row regenerates that part only. Use Lock to protect the parts you want to keep. Seed repeats a generation when all other settings are the same.

## What do the Instrument Rack controls mean?

Each row is one instrument. **S** is Solo, **M** Mute, **L** Lock. The checkbox turns the lane on. The arrows next to the sample name switch the sound. Each lane has its own level and **Strength** knob, and the pattern grid lets you edit hits by hand. The MIDI and audio buttons drag that part into your DAW. **Intensity 1–5** in the rack header sets how busy the next generation is.

## How do I set the sample and drum volume?

**Vol** in the **SAMPLE ANALYSIS** strip, next to Play w/ HPDG, sets the level of the loaded source sample: 0x to 2x, 0.80x by default.

**Rack Vol** in the **Instrument Rack** header sets the whole rack: drums and bass together, 0x to 1.50x, 1x by default. The main Vol in the top panel is linked to the same level. The source sample level is separate. Double-click a knob to reset it. The settings are saved with your DAW project and can be automated.

## How do I use my own loop or sample?

Press **Open** in SAMPLE ANALYSIS and pick an audio file. **Trim** selects the part you need. Check the BPM: you can type it in. Choose a mode and press **Analyze**.

**Guide generation** uses the analysis as a guide for generating. **Copy break (K/S/H)** tries to copy the kick/snare/hat rhythm into the pattern. It is an estimate from audio: a dense mix may need manual fixes. **Quantize** sets how strongly the result snaps to the grid. **Play w/ HPDG** plays the source together with HPDG. **Clear** removes the analysis source.

## How do I turn on the bass? Which sounds does Techno use?

Turn on **Sub808** in the rack. The **1 / 2 / 3** buttons set how much bass the next generation writes. Pick the sound with the arrows next to its name; **- / +** shift the octave.

Techno uses its own Kick, ClapGhost, HiHat, Perc and three bass samples TSB1–TSB3. Instruments missing from the Techno set fall back to sounds from other sets. Trap and Techno names can share the T prefix: they are different files in different genre folders.

## How do I get the result into my DAW?

**Export Full** saves the whole pattern as MIDI; **Drag Full** drags it onto a MIDI track. **Export Loop WAV** saves an audio loop. The drag button on an instrument row moves a single part. MIDI holds notes and events, so the receiving track needs a suitable instrument; WAV is the rendered audio.

## How do I save my settings?

Save your DAW project: the HPDG state is stored with it. Use the **Presets** menu for separate settings. If you move the project or change the source audio file, you may need to open the source again; keep sources next to your project.

## The plugin is not in the list — what now?

Check that your DAW is 64-bit and supports VST3, then rescan the standard VST3 folder. If HPDG ended up in the blocked plugins list, reset its entry and rescan after restarting the DAW. If the installer asked for a restart for the Runtime, do it.

## No sound — what should I check?

Check the transport, that the lanes are on, Solo/Mute, Rack Vol and the volume of the DAW instrument track. For source audio, also check Vol in SAMPLE ANALYSIS and Play w/ HPDG. The bundle must still contain Contents\x86_64-win\Samples with all its subfolders and sample-names.json. If the library is damaged, reinstall the beta with the DAW closed.

## Can I rename or move the factory samples?

Keep the installed structure. The sample-names.json files hold the original names, tags and bass tuning. For your own audio use Open; do not replace factory files. Uninstalling HPDG removes the installed library, so never keep your projects or sources inside HPDG.vst3.

## How do I update or uninstall the beta?

Press **Update** next to the version in the plugin header (or **HPDG → Update**). When a new version is out, the button shows **Update: <version>**. Click it, enter your license key once (it is in your purchase email) and HPDG downloads the installer, checks it and starts it after you confirm. Save your project first: the installer asks you to close the DAW. Projects and settings are kept. Without a key you can still see that a new version is out; **Buy HPDG** opens the HPDG page. Right-click the button to change or remove the key.

To uninstall, use **Windows Settings → Apps → HPDG VST3** or the uninstall shortcut in Start → HamloProd. Keep projects and exported files outside the plugin folder.

## How do I report a bug?

Send HamloProd (hello@hamloprod.org) your HPDG version, Windows version, DAW name and version, genre, substyle, BPM, Seed and the exact steps. Attach a screenshot, and the source audio if the problem is with analysis. For an installation problem, run the installer with /LOG: it writes a log to your user temp folder.

## Who owns the rights?

© 2026 HamloProd. HamloProd owns the original HPDG code and design. Third-party libraries, fonts and samples remain with their owners. Details are in **RIGHTS.txt** and **THIRD-PARTY-NOTICES.txt**, included with the installation.
