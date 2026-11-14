#!/bin/bash
# Double-click on the Mac (Kerem, 2026-11-14: "one program like before"): opens the Lisztmachine app and the visuals
# following it live. Play, the song, Improvisation and the metronome are on the visuals page; the app makes the sound
# (its window can stay behind). Close this Terminal window to stop the visuals.
cd "$(dirname "$0")" || exit 1
APP="build/Lisztmachine_artefacts/Release/Lisztmachine.app"
if [ ! -d "$APP" ]; then echo "The app is not built yet: see the README (cmake --build build --target Lisztmachine)."; exit 1; fi
open "$APP"
exec bash "visuals/Start Visuals.command" "#live"
