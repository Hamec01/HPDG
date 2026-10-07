#!/bin/bash
# typed-tempo origin check: for every labeled loop, analyze with --bpm <label> and print origin.
LAB=/c/Users/ham/Documents/DRUMENGINE/build/Release/HPDG_BreakLab.exe
for dir in "/d/Drums/Boombap/2 GB OF FREE SAMPLES/02_CUSTOM_DRUM_LOOPS_(150+Custom_Drum_Loopz)" "/d/Drums/Boombap/2 GB OF FREE SAMPLES/04_CLASSIC_DRUM_BREAKS_(80+Drm_Loopz)"; do
  for f in "$dir"/*.wav; do
    name=$(basename "$f")
    bpm=$(echo "$name" | grep -oiE '[0-9]+(\.[0-9]+)? ?B?pm' | head -1 | grep -oE '[0-9]+(\.[0-9]+)?')
    [ -z "$bpm" ] && continue
    origin=$("$LAB" analyze "$f" --bpm "$bpm" --hits 2>/dev/null | grep -m1 "Drum break:" | grep -oE 'origin [-0-9.]+' | grep -oE '[-0-9.]+$')
    echo "$origin|$bpm|$name"
  done
done
