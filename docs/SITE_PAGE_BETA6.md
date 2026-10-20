# Brief for the hamloprod.org agent: the HPDG page for Beta 6

Date: 2026-10-20. Page: https://hamloprod.org/en/vst (the HPDG product page). Owner: HamloProd.
Source of truth for every fact below: the HPDG repository (plugin code, `FAQ.md`,
`docs/BETA-RELEASE.txt`). Do not add features that are not listed here.

## What to do

1. Rewrite the HPDG page with the copy in "Page copy" below (English; the page language is English,
   like the plugin, the installer and the video).
2. Keep what already works on the page: the buy button / Stripe link, the video embed, the layout
   style. Replace outdated facts: genre list (Atmospheric is new), "no key needed" wording (now:
   no activation, the license key is for updates), version (0.1.0 Beta 6).
3. Use the new screenshots from `docs/site-beta6/` (in the HPDG repo):
   - `hpdg_beta6_dark.png` — the full plugin, Dark skin (hero / main shot)
   - `hpdg_beta6_steel.png`, `hpdg_beta6_sketch.png` — the other two skins
   - `hpdg_beta6_header_update.png` — the header with the Update button (section "Updates")
   Older shots in `Releases/HPDG_presentation_kit/images/screens/` (owner's PC, not in git) stay
   valid for genres, Guide generation, Copy break and DAW drag: `shot_boombap.png`,
   `shot_trap.png`, `shot_guide.png`, `shot_copybreak.png`, `shot_daw_drag.png`, `shot_skins.png`;
   and `Releases/HPDG_presentation_kit/Beta3_screens/` for Atmospheric, Intensity and Strength.
   Convert to WebP with a PNG fallback, lazy-load below the fold, alt text on every image.
4. Add the "What's new in Beta 6" block and the changelog (section 11) near the bottom.
5. Mark anything you cannot confirm as `[TODO owner]` instead of inventing it. HPDG is a VST3
   plugin for Windows only: do not mention or promise other formats or platforms.

Anchors the plugin links to: the page itself (`https://hamloprod.org/en/vst`) from the menu
"HPDG → Site" and from "Buy HPDG" in the license-key dialog — the buy button must be easy to find
from the top of the page.

---

## Page copy

### 1. Hero

**HPDG — HamloProd Drum Generator**
Drums and bass for your beats, generated in your DAW. VST3 for Windows.

- Five genres, 28 styles: Boom Bap, Trap, DnB, Techno, Atmospheric
- Builds a beat around your own loop or copies a drum break
- Drag MIDI or WAV straight into any DAW

Buttons: **Get HPDG** (to the buy section) · **Watch the video**

Small line under the buttons: *Version 0.1.0 Beta 6 · Windows 10/11 64-bit · VST3 · no activation*

### 2. What HPDG is

HPDG writes drum patterns and a bass line for you. Pick a genre and a style, press
**Generate Pattern**, and a full beat appears in the grid: kick, snare, hats, percussion, cymbals
and an 808 or bass line, already playing with its own sounds. Listen, change what you don't like —
one lane or the whole beat — and drag the result into your DAW as MIDI or audio.

Every style has its own rules for groove, swing, density and fills, so each click gives a new beat
in the same style, not random notes.

### 3. Genres and styles

| Genre | Styles |
|---|---|
| **Boom Bap** | Classic, Dusty, Jazzy, Boom Bap Gold, Russian Underground, Lo-fi Rap |
| **Trap** | ATL Classic, Dark, Cloud, Rage, Memphis, Luxury |
| **DnB** | Modern, Roller, Liquid, Neurofunk, Jump-Up, Breakbeat |
| **Techno** | Peak Time, Hypnotic, Minimal, Detroit, Dub, Acid, Hard |
| **Atmospheric** | Pulse, Two-Step, Broken |

- **Trap:** hi-hat rolls and triplets, 808 lines across the whole phrase with glides, half-time
  snares.
- **Techno:** four-on-the-floor kick, off-beat hats, percussion, its own bass sounds
  (including a tech-house kit).
- **Atmospheric:** Pulse, Two-Step and Broken rhythms with a melodic bass over minor harmony and its
  own drum and bass kit.

### 4. Start from your sample (Sample Analysis)

Drop a loop or a sample into **Sample Analysis** (WAV, AIFF, FLAC, MP3). HPDG measures its tempo,
swing and key and finds the bass notes, then builds the beat for that sample: the same tempo, a bass
line in the right key, a groove that sits with the loop. **Play w/ HPDG** plays your loop and the
beat together inside the plugin, with its own volume, before you export anything.

- **Trim** — pick the part of the file you need: Loop / Play / Stop, drag the edges, or type
  "Selection = N bars" and the tempo follows.
- **Guide generation** (default) — the analysis guides the generator.
- **Copy break (K/S/H)** — drop a drum break and HPDG transcribes it hit by hit into Kick, Snare and
  Hi-Hat lanes with the original timing and swing. Then swap the sounds, quantize as much as you
  like (**Quantize**), add a bass line, mutate it.

### 5. Control every lane (Instrument Rack)

Each instrument is one lane: HiHat, Hat Accent, OpenHat, Snare, Kick, Kick Ghost, Ride, Cymbal,
Sub808 — add more with **+ Lane**.

- **RG** regenerates one lane and keeps the rest; **Mutate** makes a variation of the whole beat.
- **S** Solo, **M** Mute, **L** Lock (protect a lane from changes), **C** Clear.
- **< >** flip through the kit sounds of that lane.
- A level knob and a **Strength** knob per lane: left removes the weakest hits, right adds hits
  typical for the genre. On Trap hats it removes whole rolls first and keeps the quarter notes.
- **Intensity 1–5** for the whole rack: 1 is the simplest beat, 5 the busiest. The next Generate,
  Mutate or RG uses it.
- **Sub808:** bass amount **1 / 2 / 3** for the next generation, **– / +** shift the octave (Shift —
  a semitone; MIDI export keeps it). By default it plays HPDG's own synth sub (4 variants).
- **Rack Vol** for the whole kit; the pattern grid lets you draw and edit hits by hand.

### 6. Shape the groove

Header controls: **BPM** (with Lock and **Sync** to the DAW tempo), **Swing**, **Density**, **Bars**
(1, 2, 4, 8, 16), **Tempo Mode** (Auto, Original, Half-time), **Snap** (1/4 to 1/64, triplets,
Adaptive, Micro), **Seed** (repeat a generation exactly), **Playback** (Play Flag or Loop Range),
**Start play with DAW**. Advanced view adds **Velocity**, **Timing** and **Humanize**.

### 7. Into your DAW

- **Drag Full** — drag the whole pattern onto a MIDI track. **Export Full** saves it as a MIDI file.
- The **MIDI** button on a lane drags just that part; the **WAV** button drags it rendered with its
  sound.
- **Export Loop WAV** saves the full mix as an audio loop.
- While you drag, the plugin window steps aside once the file leaves it (FL Studio and others).
- Everything is saved with your DAW project; **Presets** keep settings you like.

### 8. Three skins

Dark, Steel and Sketch — switch any time from the **SKIN** menu.

### 9. Updates inside the plugin (new in Beta 5–6)

Next to the version in the header there is a small **Update** button.

- Click it: HPDG asks hamloprod.org whether a new version is out — **Latest**, **Update: <version>**
  or **Offline**.
- When a new version is out, click again and enter your **license key** once. HPDG downloads the
  installer, checks that it is complete and unchanged (SHA-256), and starts it when you confirm.
  Save your project first: the installer asks you to close the DAW.
- Click **HPDG** in the header for the menu: **Update**, **Instruction** (the guide), **Site**.
- No internet? HPDG works exactly as before.

**Your license key** looks like `HPDG-XXXX-XXXX-XXXX-XXXX` and comes by email to the address you used
to buy HPDG. It is only for updates — every feature works without it, and there is no activation.
Lost it? Write to hello@hamloprod.org from your purchase email.

### 10. Requirements and install

- Windows 10 / 11, 64-bit; a 64-bit DAW with VST3 support (FL Studio, Ableton Live, Reaper,
  Studio One, Cubase, Bitwig… `[TODO owner: list only DAWs you have tested]`).
- One installer (~110 MB): the plugin, 913 factory samples and the Visual C++ Runtime. Nothing else
  to download, no account.
- Installs to `C:\Program Files\Common Files\VST3\HPDG.vst3`; rescan VST3 plugins in your DAW.
- Format: VST3 plugin only, Windows only.

### 11. What's new

**Beta 6** — the installer and the guide in English; the FAQ explains the license key.

**Beta 5** — the Update button and in-plugin updates with your license key; the HPDG menu
(Update, Instruction, Site).

**Beta 3** — new genre Atmospheric (64 new samples); Intensity 1–5; Strength knob on every lane;
Trap 808 in the right register with glides; HPDG synth sub by default; the window steps aside while
you drag into the DAW; Guide generation by default.

**Beta 2** — Trim: Loop / Play / Stop and "Selection = N bars"; Sub808 plays notes as written with
octave buttons; Dark and Steel skins; a second Techno kit (100 new samples).

### 12. Buy

**HPDG 0.1.0 Beta 6** — `[TODO owner: price and what is included — e.g. beta now + updates]`.
After the payment you get the installer and your license key by email
`[TODO site: true only after the Stripe webhook and email are live; until then: "within 24 hours"]`.
Questions: hello@hamloprod.org.

Honest note: HPDG is in beta. Keep backups of your projects and tell us about bugs (HPDG version,
Windows, DAW, genre, style, BPM, Seed and the steps).

### 13. FAQ (short; the full guide ships with the plugin)

- **Does it need internet or activation?** No. The Update button uses the internet only when you
  check for or download an update.
- **Which DAWs?** Any 64-bit Windows DAW that loads VST3.
- **Mac?** Not yet.
- **Can I use the beats in my releases?** The music you make with HPDG is yours.
  `[TODO owner: confirm the wording about samples in commercial releases]`
- **How do I update?** Press Update in the plugin header. Or run the new installer over the old
  one — projects and settings stay.
- **Uninstall?** Windows Settings → Apps → HPDG VST3.

Footer: © 2026 HamloProd · Rights and third-party notices (RIGHTS.txt, THIRD-PARTY-NOTICES.txt ship
with the plugin) · hello@hamloprod.org

---

## Video

The current demo (`HPDG_demo.mp4`, Beta 1, 3:41) predates Atmospheric, Intensity, Strength and the
Update button. Keep it until a new one is ready; a Beta 6 video can be cut from the plugin with the
HPDG demo-reel tools (`tools/demo_reel`) — separate task.
