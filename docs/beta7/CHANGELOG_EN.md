# HPDG Beta 7 - guide and changes

Text version of the English PDF.

## Page 1

HAMLOPROD / HPDG
A NEW CHAPTER
BETA 7
Your guide to the update, new features and two distinct Trap characters. 6 November 2026
MORE CHARACTER.
MORE CONTROL.
From spacious ATL grooves to dark bass phrases: Beta 7 develops the musical
rules behind generation, adds hands-on 808 control and improves the sample
workflow.
Inside this release
ATLClassic and DarkTrap • HPDG 808 inside Sub808 • Trap Rolls • Basic Pitch • audio fixes
1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
HI-HAT
SNARE
KICK
808
An illustration of the musical idea, not a fixed generator preset.
Thank you for your support!
I am grateful for your support of HPDG and look forward to your feedback. You can always write to me
about what you would like to improve: the sound, generation, interface or workflow. Your ideas help
shape the plugin.
HamloProd · hello@hamloprod.org

## Page 2

HAMLOPROD / HPDG
01 / WHAT'S NEW
Everything since Beta 6
The additions and fixes since the published Beta 6 release of 20 October.
01 Two distinct Trap characters
ATLClassic favours sparse kicks, a straight hi-hat foundation and independent 808 answers.
DarkTrap brings darker bass, sustained anchors, short pickups and phrase endings. These are two
substyles within Trap; the other substyles remain available.
02 Rhythm and sample guidance
Sample-guided kick timing has been refined. Trap kicks avoid the snare and its immediate aftermath.
ATL/Dark bass stays in a compact register. In DarkTrap, a short pickup follows the pitch of the next
confidently detected bass anchor.
03 Rolls and a more precise grid
Trap Rolls offers Off, Style and Fills. Fills adds short bursts at phrase endings. The grid displays the
selected subdivision and triplets more accurately, with distinct roll notes. Selecting Trap sets 1/32; the
other main genres use 1/16. You can override the grid selection.
04 A new Sub808
Sub808 now uses the HPDG 808 engine. There are six new factory voices, ATL 1-3 and Dark 1-3,
plus a sound strip/editor with a sample layer, filters, drive, envelopes and presets. Drive level
compensation helps compare sounds, and presets can be saved from the editor. Boom Bap bass is
enabled by default.

## Page 3

HAMLOPROD / HPDG
02 / MORE CHANGES
Analysis and smoother audio
The rest of the Beta 7 feature update.
05 Key estimation from notes
Sample Analysis uses Basic Pitch notes to estimate the musical key. If no notes are found, it falls
back to the previous chroma analysis. A manual key selection still takes priority. Treat the estimate as
a guide, rather than a guarantee for every audio file.
06 Smoother audio behaviour
Fixes address Sub808 note-end crackle and crackle while turning controls. UI state reads interfere
less with the audio thread, and state saving avoids a long audio-lock hold. Heavy load and some
editing operations may still cause interruptions.
What the installer includes
HPDG VST3 for Windows 10/11 x64 and 919 factory audio samples. HPDG needs no activation or
constant internet connection to make music; your license key is used for updates.

## Page 4

HAMLOPROD / HPDG
03 / TWO TRAP CHARACTERS
ATL Classic and Dark Trap
Both use a half-time foundation. Their musical behaviour differs, not just the 808 tone.
ATLClassic / a conversation with the bass
Sparse kicks leave room for the 808. Straight eighth-note hats provide the foundation, with restrained
rolls in Style mode. The bass answers between kicks and tends to repeat its anchor rather than jump
through octaves.
1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
HI-HAT
SNARE
KICK
808
DarkTrap / weight, space and a pickup
Darker low end and sustained notes. The kick and 808 can move independently. A short bass pickup
leads into a longer anchor, with variation unfolding toward the phrase ending. Hats can be even or use
alternating two-step accents.
1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
HI-HAT
SNARE
KICK
808
These are teaching diagrams: 16 cells = one 4/4 bar, with the snare on beat 3 (cell 9). They illustrate the idea, not a
promised Generate result. Rolls and note lengths are simplified.

## Page 5

HAMLOPROD / HPDG
04 / HOW IT WORKS
A little musical maths
Probability, musical constraints and candidate scoring - without copying a finished beat.
1. Start with several possibilities
The generator creates multiple patterns, repairs violations and assesses musical quality and style fit.
Seed controls repeatability; Density and Variation influence the events it chooses.
P(extra kick) = (0.30 + 0.45D + 0.20I) × S
The first extra-kick formula: D = internal density; I = kick irregularity.
S = 0.45 for ATLClassic; S = 0.35 for DarkTrap. With D = 0.5 and I = 0.5, this is about 28% versus
22%. It is the chance of an attempt to add a kick, not a proportion of all hits. An occupied or forbidden
position is still rejected. These two styles allow at most one such addition per bar.
Qselect = Q + 0.06 × (Fit - 0.5)
The implemented selection score: Q = pattern quality; Fit = match to the target style.
The style adjustment is small: Fit from 0 to 1 changes the score by -0.03 to +0.03. The selection
mechanism also considers thresholds and variety. A high style fit does not mean every result will be
the same.
2. Musical rules constrain chance
The snare establishes the half-time anchor. Checks prevent unwanted kick/snare collisions and
contain the 808 register. Pauses, repeats and phrase endings follow the substyle rules.
These formulas explain the generator, not controls with these exact names. The numerical example is illustrative.

## Page 6

HAMLOPROD / HPDG
05 / FROM GRID TO MUSIC
Time, rolls and pitch
Three simple relationships explain the timing and movement you hear.
Quarter note = 60 / BPM seconds
At 150 BPM: one quarter note = 0.4 s; one 4/4 bar = 1.6 s.
At 150 BPM, a snare on beat 3 lands 0.8 seconds into the bar. Both 75 and 150 BPM can describe the
same half-time groove using different grids. Check the sample BPM and selection length; avoid
doubling the tempo again without a reason.
1/32 step = quarter note / 8
At 150 BPM: 1/16 = 100 ms; 1/32 = 50 ms; 1/32T = 33.3 ms.
Trap Rolls: Off disables generated rolls. Style keeps the substyle character. Fills adds a short burst at
the ending. Snap controls editing and display; it does not automatically straighten generated swing or
triplets. Low Intensity or reduced hat Strength can remove some roll notes.
f = 440 × 2^((MIDI - 69) / 12)
One octave up = frequency × 2. Gate duration = beats × 60 / BPM.
A one-quarter gate at 150 BPM lasts 0.4 seconds. The sound can continue fading after Note Off: that
is Release. Glide changes the transition between note pitches. Pitch Attack is a short pitch drop at the
start of a sound. They serve different musical purposes.
Octave labels vary between DAWs. When the bass register feels wrong, use your ears and MIDI note numbers as well as
labels such as C1/C2.

## Page 7

HAMLOPROD / HPDG
06 / HANDS ON
Shape your Trap sound
Begin with a simple pattern, then change one thing at a time.
1 / Choose the character
Select Genre: Trap, then ATLClassic or DarkTrap. Start with four bars, Intensity 3 and neutral hi-hat
Strength. Set the project tempo; 140-150 BPM is a useful starting point for comparing these patterns.
2 / Listen to Style first
Set Trap Rolls to Style, then Generate. Compare a few seeds. For a more pronounced ending,
choose Fills and regenerate the pattern or hats. Choose Off when you want a clean foundation without
generated rolls.
3 / Balance the 808
Open the Sub808 strip/editor. Choose a preset, then balance its level against the kick. Add Drive
gradually and soften the top with a filter. Compare tones at similar loudness; Drive compensation does
not replace checking the balance by ear.
4 / Separate envelope jobs
Attack softens the start, Sustain holds the body and Release controls the fade after the note ends. A
long Release may fill gaps. Glide creates a pitch transition between notes; check the bass without it
first, then add it to taste.
5 / Save it and take it into your DAW
Save your 808 preset. Drag MIDI or WAV into the DAW and listen in the arrangement. MIDI carries
notes and durations; the receiving instrument defines the final tone, so it may sound different from
HPDG.

## Page 8

HAMLOPROD / HPDG
07 / SAMPLE ANALYSIS
Build around your sample
Guide creates a new pattern informed by analysis; Copy Break has a different purpose.
1 / Check the source loop
Load your musical sample. In Trim, select the section you need and set Selection = N bars if you
know its length. Listen to the loop boundaries and check that the detected tempo matches the
material.
2 / Check the key
Basic Pitch extracts notes used to estimate the key. With no notes, analysis falls back to chroma. If
the estimate is wrong, select the key manually: your choice takes priority. Noise and complex mixed
audio may not produce a reliable result.
3 / Choose Guide
Use Guide and your Trap substyle to create a new beat around a musical loop. In DarkTrap,
confidently detected bass anchors help align the pitches of short pickups. Weak analysis should not
force pitch changes.
4 / Know Guide versus Copy Break
Guide keeps the generator's musical logic and can preserve a hat ending in Fills mode. Copy Break
transfers an estimated drum rhythm; it is not a command to compose a new beat in a chosen style.
5 / Listen in context
Compare the result with the source: does the tempo fit, does the bass agree with the harmony, is
there enough space, and are the rolls too busy? Lock a successful lane before generating again.
Adjust key, octave or note lengths when needed.

## Page 9

HAMLOPROD / HPDG
08 / INSTALLATION
How to update correctly
Save your project and a backup first. Beta 7 installs over HPDG.
From HPDG / the recommended sequence
1. Open HPDG in your DAW and press Update next to the version.
2. When Beta 7 is available, enter the license key from your purchase email if prompted. It grants
update access; it does not activate the audio engine.
3. Wait for the download. HPDG verifies the installer's SHA-256.
4. Confirm the installer launch. Close the DAW before files are replaced, and follow any installer
request to close applications.
5. Finish installing, restart the DAW and rescan VST3 if needed. Check that the HPDG header shows
0.1.0 Beta 7.
Manually / if Update is unavailable
Visit https://hamloprod.org/en/vst and obtain the installer with your key, following the site's
instructions. Close the DAW and run HPDG_VST3_Setup_0.1.0-beta.7.exe. Finish installation,
reopen the DAW and check the version. If the site still shows Beta 6, Beta 7 has not yet been
published.
If something goes wrong
Latest while running Beta 6: check whether Beta 7 is published.
Offline: check your connection and retry later; HPDG itself works offline.
Key rejected: compare it with your purchase email. Never share it publicly.
Old version still showing: restart the DAW, rescan VST3 and check for another HPDG copy in a
custom VST3 folder.
Default folder: C:\Program Files\Common Files\VST3\HPDG.vst3. Keep the library inside the bundle. Need help?
hello@hamloprod.org

## Page 10

HAMLOPROD / HPDG
09 / STAY IN TOUCH
Thank you for supporting HPDG
The beta grows through people using it in real projects.
I am grateful for your support and look forward to your feedback.
You can always write to me about what you would like to improve: the character of generation, 808
sound, sample workflow, interface or the way HPDG fits into your DAW. Tell me what worked, what
got in the way and what you would like to see next.
Help me reproduce the problem
Include your HPDG version, Windows and DAW version, genre/substyle, BPM, seed and the steps to
reproduce. A short audio example helps with a sound issue; a screenshot helps with the interface.
Never post your license key. Only share projects or third-party material you have permission to share.
Contact HamloProd
hello@hamloprod.org
Plugin page: https://hamloprod.org/en/vst
About the examples
The rhythm diagrams are teaching examples, not fixed presets or promised Generate results. The
formulas explain how timing, pitch and musical choices work. Results vary with the seed, settings,
source sample and musical context. Always listen to the generated pattern in your arrangement.
Prepared for 0.1.0-beta.7. Installer publication and buyer notifications are separate steps after the release package is
checked.

