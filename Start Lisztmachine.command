#!/bin/bash
# Double-click on the Mac (Kerem, 2026-11-14: "one program like before"): opens the Lisztmachine app and the visuals
# following it live. Play, the song, Improvisation and the metronome are on the visuals page; the app makes the sound
# (its window can stay behind). Close this Terminal window to stop the visuals.
cd "$(dirname "$0")" || exit 1
APP="build/Lisztmachine_artefacts/Release/Lisztmachine.app"
if [ ! -d "$APP" ]; then echo "The app is not built yet: see the README (cmake --build build --target Lisztmachine)."; exit 1; fi
# an app still open from before would only come to the front, old code and all (2026-11-18): it is closed first
osascript -e 'quit app "Lisztmachine"' >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -x Lisztmachine >/dev/null || break; sleep 0.5; done
open "$APP"
exec bash "visuals/Start Visuals.command" "#live"
