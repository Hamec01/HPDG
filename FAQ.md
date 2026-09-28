# HPDG — Frequently Asked Questions

**HamloProd Drum Generator** — a VST3 / Standalone drum pattern generator by BoomBap Labs.

For a visual, control-by-control walkthrough of the interface, see the HPDG Field Guide.

---

## General

### What is HPDG?

HPDG generates complete drum patterns — kick, snare, hats, ghost lanes, texture layers — from a genre and a feel, rather than a fixed set of loops. Each genre runs its own generation engine tuned to that style's pocket, and the result is real MIDI you can drag straight into your DAW, not audio.

### What genres are available right now?

**Boom Bap** and **Trap**, each with several substyles (for example Boom Bap's Classic, Dusty, Jazzy, Boom Bap Gold, Russian Underground and Lofi Rap pockets).

**Rap** and **Drill** exist in the engine but are held back from the interface in this release while they get more tuning. They are not deleted or degraded — a future update re-enables them for everyone on the same install, no reinstall or repurchase needed.

### Is this a sample player or a MIDI generator?

Both, layered. HPDG assigns a sample to every lane so you can preview and print audio directly from the plugin, but the underlying pattern is MIDI. You're free to mute the built-in sounds and drag the MIDI onto your own drum rack, sampler or kit instead.

### Does it work outside a DAW?

Yes — a Standalone build is included alongside the VST3, for auditioning patterns or building ideas without a host loaded.

---

## Installation

### What do I need to run it?

- Windows 10 or later, 64-bit
- A VST3-compatible host (FL Studio, Ableton, Reaper, Cubase, Studio One, etc.) for the plugin, and/or nothing at all for the Standalone app
- No separate sample packs to install — the factory kits ship with the plugin

### Where does the installer put things?

- The VST3 goes to the shared `Common Files\VST3` folder so every VST3 host on the machine can see it
- The Standalone app goes to `Program Files\HPDG`
- Your own generated patterns, exports and Style Lab captures are never touched by install or uninstall — they live under your Documents folder

### My DAW doesn't see the plugin after installing

Rescan your plugin folders (most hosts have a "rescan" or "find new plugins" action) — most VST3 hosts only pick up new plugins in the shared VST3 folder automatically, but a small number cache their scan results and need an explicit rescan. If you installed while your DAW was open, close and reopen it first.

### Can I install it without admin rights?

Installing into the shared VST3 folder needs administrator rights, since that folder is shared across all users on the machine. If you don't have them, install for your Windows user account only when the installer offers that option.

---

## Using HPDG

### Why does the plugin look different in FL Studio than in other hosts?

FL Studio has a known scanning quirk with certain plugin UI patterns that can hang the scan step. HPDG detects this and falls back to a streamlined header-only view when it's loaded there, to guarantee it always loads reliably. Every generation feature — genre, substyle, swing, density, seed, per-lane controls — is still there; only the layout is more compact. In hosts without that quirk, you get the full rack-and-grid view shown in the field guide.

### What does Seed actually control?

Seed makes generation reproducible. The same Genre, Substyle, Swing, Density and Seed will always produce the exact same pattern — useful for coming back to an idea later, or for comparing two settings fairly, without the randomness that "Generate" normally introduces each time.

### What's the difference between Generate and Mutate?

**Generate** replaces the pattern from scratch. **Mutate** takes what's already in the rack and nudges it — new fills, slightly different accents — while keeping the overall shape, which is usually what you want once you've found a pocket you like.

### How do Swing and Density interact with substyle?

Each substyle already has its own baseline feel — Boom Bap Dusty sits further behind the beat than Boom Bap Classic, for instance. Swing and Density are applied on top of that baseline, so the same knob position reads differently from one substyle to another by design.

### Can I edit a generated pattern by hand?

Yes. Every lane is a normal step grid — click to add or remove hits, drag to move them, use Snap to change the grid resolution you're editing against. Regenerating a single lane (the per-lane RG button) only touches that lane, leaving your manual edits elsewhere untouched.

### How do I get a pattern out of HPDG?

Drag-and-drop is the fastest path: **Drag Full** exports the whole pattern as MIDI directly onto a track, and each lane has its own **Drag** for just that instrument. **Export Full** and **Export Loop WAV** write files to disk instead, as MIDI or rendered audio, if you'd rather work from the file system.

### Does HPDG sync to my DAW's tempo?

Yes, when **Sync** is enabled the plugin follows your host's tempo and transport. Turn on **Lock** if you want to pin HPDG's BPM independently of host tempo changes, and **Start play with DAW** if you want HPDG's preview playback to start and stop with your host transport automatically.

### What's Style Lab?

Style Lab lets HPDG learn from a reference — you feed it an existing pattern or recording, and it captures the hi-hat and kick placement, density and feel to influence future generations in that substyle. It's a way to steer the generator toward a specific reference without hand-tuning every parameter yourself.

---

## Sound & Samples

### Can I use my own samples?

Yes — each lane's sample browser isn't locked to the factory kit; point it at your own one-shots and HPDG generates around them exactly as it would around the built-in sounds.

### The generated audio sounds quiet or clipped

Check the **Output** trim in the top bar first — it's a master gain stage across every lane. If individual lanes fight each other, each lane also carries its own level knob in the instrument rack.

---

## Troubleshooting

### Generation feels slow the first time I click it

The first "Generate" after loading the plugin (or after switching to a substyle you haven't used yet) does a bit of one-time setup work reading that substyle's saved reference data; it's cached immediately afterward, so every following generation on the same substyle is fast. Switching substyles pays that cost again the first time, not on every click.

### A pattern looks different from what I remember at the same settings

Generation is seeded randomness unless you fix the **Seed** field — without a fixed seed, Generate intentionally produces a new variation each time, even with identical Genre/Substyle/Swing/Density. Set a Seed if you need exact repeatability.

### The plugin won't load / my host reports an error

Confirm you're running a 64-bit host — HPDG is 64-bit only. If the problem is specific to one host, try the Standalone app to confirm the plugin itself is working, then rescan plugins in the host.

---

## Licensing & Support

### Who makes HPDG?

BoomBap Labs.

### Where do I report a bug or request a feature?

Include your HPDG build number (shown under the logo in the top-left of the plugin) and, if possible, the Genre/Substyle/Seed that reproduces the issue — that combination alone is usually enough for us to reproduce a pattern exactly on our end.
