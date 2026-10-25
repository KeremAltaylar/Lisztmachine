#!/bin/bash
# Double-click on the Mac: serves the visuals from this folder and opens them in Chrome, where the
# page may use the audio interface (the drum pad). Close this window to stop.
cd "$(dirname "$0")" || exit 1
PORT=8731
python3 -m http.server "$PORT" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
if [ -d "/Applications/Google Chrome.app" ]; then
  open -a "Google Chrome" "http://127.0.0.1:$PORT/"
else
  open "http://127.0.0.1:$PORT/"
fi
echo "Lisztmachine visuals: http://127.0.0.1:$PORT/  (close this window to stop)"
wait $SERVER
