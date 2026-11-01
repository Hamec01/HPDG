# Tempo Mode

The BPM control is the source tempo. With Sync enabled, the source is the DAW BPM.
The header displays `source -> musical BPM`. HPDG never changes the DAW tempo.

| Trap source | Auto | Original | Half-time | Auto + x2 |
| --- | --- | --- | --- | --- |
| 70 | 140 | 70 | 35 | 280 |
| 90 | 180 | 90 | 45 | 360 |
| 140 | 140 | 140 | 70 | 280 |
| 180 | 180 | 180 | 90 | 360 |

Auto interprets a low tempo in the genre's musical range. Original uses the source
directly. Half-time divides the source by two. The independent x2 button doubles
the result of the chosen mode. These controls work with both manual BPM and Sync.

Generation, playback, sample mapping and MIDI export use the resulting clock.
The Trap engine does not fold an already resolved clock a second time. The Bars
control counts HPDG musical bars: eight bars at 180 take the same real time
whether the host reports 90 or 180. MIDI export adjusts its tick resolution to
retain that duration when imported back into the host.

Changing a mode or x2 changes playback speed immediately and keeps the existing
notes. Generate creates a new pattern at the selected musical tempo. Use the same
locked seed and settings to compare Trap Auto at 70/140 or 90/180.

The new x2 parameter is saved with projects and presets. Existing Tempo Mode
choice indices remain Auto=0, Original=1, Half-time=2. Half-time now means an
actual half-speed clock instead of the previous tempo-band hint.
