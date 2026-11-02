---
name: hpdg-video
description: Make a short HPDG promo/tutorial video for YouTube from a raw OBS screen recording of FL Studio — cut, speed-ups, captions, logo intro/outro, voice-over mixing, 9:16 Shorts version, thumbnails, and the YouTube title/description/tags. Use when the user sends a recording (C:\Users\Ham_h\Videos\*.mp4) and asks for a video, a voice-over text, a Shorts version or a thumbnail.
---

# HPDG video pipeline

The owner records FL Studio with OBS (1280x720, 30 fps), sends the path, and expects back:
a finished short video, a voice-over script **without timecodes**, then (after he sends the
generated voice .wav) the voiced video, a thumbnail and YouTube texts.

Language: **chat with the owner in Russian; everything inside the video, the voice-over
script, thumbnails and YouTube texts is in English.**

All the mechanics are in one tool: `tools/video/hpdg_video.py` (ffmpeg comes from the
`imageio-ffmpeg` pip package, fonts from C:\Windows\Fonts, logo from
`Releases/HPDG_presentation_kit/logo/logo_red_1024.png`). A video is described by a JSON
job — copy `tools/video/jobs/example_sample_to_patterns.json` and edit it.
Outputs go to `Releases/HPDG_presentation_kit/video/` (thumbnails into `.../video/thumbnails/`);
that folder is git-ignored, do not commit videos.

```
python tools/video/hpdg_video.py inspect  <rec.mp4> <dir> [step_s]   # contact sheets + loudness
python tools/video/hpdg_video.py frame    <rec.mp4> <sec> <out.png>   # one full 1280x720 frame
python tools/video/hpdg_video.py build    <job.json>                  # horizontal 1920x1080 video
python tools/video/hpdg_video.py pauses   <voice.wav>                 # pauses in a voice-over
python tools/video/hpdg_video.py voice    <job.json>                  # voiced video
python tools/video/hpdg_video.py vertical <job.json>                  # 1080x1920 Shorts
python tools/video/hpdg_video.py thumb    <job.json>                  # 1280x720 thumbnail(s)
```

Rendering long videos takes minutes — run `build` in the background for recordings > 5 min.

## 1. Understand the recording (never guess)

1. `inspect <rec> <scratch>/insp 4` (use 2–3 s steps for short recordings). Read every
   `sheet_*.jpg`. Note what happens when: chopping a sample (Serato Sample), writing notes,
   rendering, HPDG Sample Analysis / Trim / Generate / substyle / Intensity / Drag, piano roll,
   arrangement, final playback, OBS windows, browser, other plugins.
2. The loudness line shows where music plays (-12…-16 dB) and where it is silent (-inf).
   The **final beat** is usually the last long loud stretch.
3. Before writing captions/text about specific values, `frame` the moment and read it zoomed
   (crop with PIL and Read the PNG): genre, substyle, BPM, bars, key, swing in the Sample
   Analysis line (e.g. "139.0 BPM (typed) | 8 bars | swing 55% … key C major").
   Only state what is visible. If unsure what an action does, say so to the owner in the reply
   and phrase the line neutrally.

## 2. Plan the cut

Rules the owner has confirmed:
- **Crop**: the default `crop` [200, 0, 1080, 690] removes FL's file browser (left) and the
  Windows taskbar (bottom) — the owner must never see folders/taskbar. Check one frame; if the
  FL window is laid out differently, adjust `[x, y, w, h]` in 1280x720 source pixels.
- Cut out: OBS windows, browser pages (ads, tabs), Trim/other dialogs left open, dead time,
  repeated tries.
- **Speed up** boring parts (placing notes, choosing sounds, chopping, rendering, dragging,
  arranging): speed 2–35. The badge "▶▶ xN" is added automatically. If the owner says
  "прям быстро" use x20–x35.
- Show at real speed (speed 1): the sample/melody playing, Generate → Play, new variations,
  substyle/Intensity changes, Drag Full, the finished beat.
- **No silence under speed-ups when the owner asks for it** ("бит должен быть слышен"): set
  `"bed": <second in the recording where the finished beat plays>`; sped-up segments then play
  the finished beat underneath, continuing from that point. Without `bed`, sped-up parts are silent.
- Target length: 1:20–3:00 for tutorials; if the owner gives a length ("1.20", "не больше 40
  секунд") hit it exactly — shorten intro/outro (`intro`/`outro` seconds, 2.5 minimum) and
  trim segments. Defaults: intro 4 s, outro 4.5 s; make the intro 6–7.5 s if the first voice
  line is long.
- Captions: short English, Title-ish, ≤ 45 chars ("Drop the loop into Sample Analysis",
  "Trap → Generate → Play", "83 BPM · F minor → Genre: Trap").
- Credit third-party loops on screen (e.g. "Loop: Trap MelloTron Flute by Raymo123 (looperman)")
  and in the description with a link.

`segments` = `[start_s, end_s, speed, "caption"]` in **source** seconds. Run `build`; it prints
the timeline of the finished cut (start of every segment) — keep it, you need it for the voice
and for chapters. Verify: grab 4–6 frames from the output (`frame` on the output file) and look
at them; check loudness of the output if the beat must be audible.

## 3. Voice-over script (give it WITHOUT timecodes)

- One short paragraph per scene, in scene order, separated by blank lines. The owner reads it in
  a TTS generator and leaves a pause between paragraphs; you cut by those pauses.
- ~2.5 words per second of the scene it covers; fast videos get very short lines.
- First line: "HPDG — HamloProd Drum Generator. <what this video shows>."
  Last line: "HPDG. Get the beta at hamloprod.org."
- Describe what the owner does and why ("…so the drums follow my sample"). No hype words
  about features you cannot see.
- Save it next to the video as `<video>_voiceover.txt` and paste it in the chat in a code block.

## 4. Mixing the voice the owner sends

1. `pauses <voice.wav>` (try `-38` dB / 0.3 s; for the tail use 0.12 s). Speech between pauses
   = one paragraph. Long pauses (> 0.6 s) are paragraph breaks; short ones (0.3–0.5 s) are often
   inside a paragraph — match the count of chunks to the count of paragraphs by their lengths.
   The owner may have edited the text; trust the audio, not your script.
2. Fill `voice.clips` = `[voice_start, voice_end, place_at_in_video, "label"]`; place each line
   at the start of its scene from the build timeline (+0.1–0.3 s). Lines must not overlap the next
   line; a long first line may need a longer intro (rebuild with a bigger `intro`, shift all).
3. `voice` writes `<video>_voiced.mp4`: music is side-chain ducked under the voice, full volume
   between lines. Tell the owner the table "time → line" and which places you are unsure about —
   you cannot listen. When he says a line is at the wrong time, re-check the chunk mapping (the
   usual bug is two chunks merged/split), fix `clips`, re-run `voice`.
4. If he asks to speed a part up after voicing: change that segment's speed, re-run `build`,
   shift every clip after it by the saved seconds, re-run `voice`, and give updated chapters.

## 5. Shorts (9:16)

Only for videos ≤ 60 s (or a 30–45 s cut of the best moments, all real-time so the beat is
heard). Fill `vertical.segments` = `[start, end, ["LINE 1", "LINE 2"], [[x0,y0,x1,y1], ...]]`:
one or two crop boxes in source pixels stacked top to bottom (typically the whole HPDG window,
then a zoom of its Pattern grid; the arrangement view alone for the playlist scene). Find boxes
on a frame with a 100 px grid (`drawgrid`), keep them per window position (the HPDG window moves
between takes). Captions: 2 lines, UPPERCASE, ≤ 16 chars each.

## 6. Thumbnails (1280x720)

Default is `"style": "bold"` — the owner wants it to pop:
- huge Impact headline, line 1 white, line 2 yellow, thick black stroke (2–3 words per line:
  "SAMPLE IN / BEAT OUT", "TRAP BEAT / IN 1 MINUTE", "CHOP → / TRAP BEAT");
- the HPDG Pattern grid (box = the Pattern area of a frame where a full pattern is visible),
  tilted, red frame and glow;
- optional "YOUR SAMPLE" card with the real waveform + yellow arrow (`wave_at` = second where
  the sample plays alone);
- red sticker top-right (≤ 12 chars: "DRUMS + 808", "+ HPEQ PREVIEW", "FL STUDIO").
`"style": "plain"` (`lines` + `tag`) is the older calmer layout. Always Read the PNG after
rendering and re-crop if the box shows the wrong area (taskbar, empty playlist). Offer a B
variant for YouTube A/B testing.

## 7. YouTube texts (give them in the chat, also save `<video>_youtube.txt` for big uploads)

- **Title**: ≤ 70 chars, hook first: "Trap Beat in 1 Minute from a Chopped Sample | HPDG Drum
  Generator (FL Studio)". Shorts: add `#shorts`.
- **Description**: 2 hook sentences (what goes in → what comes out), what the video shows,
  `🥁 Get the HPDG beta: https://hamloprod.org/en/vst`, loop credits with links,
  `⏱ Chapters` from the build timeline (first chapter 0:00, every chapter ≥ 10 s, merge short
  scenes), footer "HPDG — HamloProd Drum Generator. VST3 drum & 808 generator for Boom Bap,
  Trap, DnB and Techno (Windows).", 3 hashtags.
- **Tags**: ≤ 500 chars, genre + "how to make …" + "fl studio …" + drum generator / ai drum
  generator / vst drum generator + HPDG, HamloProd.
- Settings: playlist "HPDG Tutorials"; not made for kids; paid promotion **No** (own product);
  altered/synthetic content **Yes** (AI voice); language English; category Music.
- Pinned comment: a question + the beta link.
- Warn about Content ID if the sample is from a commercial song; credit Looperman authors.

## Checklist before replying

- [ ] watched every contact sheet; values in captions/text read from zoomed frames
- [ ] no folders/taskbar/browser/OBS in the output frames
- [ ] length matches what the owner asked; beat audible where he asked
- [ ] voice script has no timecodes; voiced cut: table of line → time, doubtful spots named
- [ ] thumbnail Read and checked; files linked as markdown links
