#!/bin/bash
# Lisztmachine on a new Mac (Apple silicon), in one command (Kerem, 2026-11-14: "install this with a command to another
# computer"):
#
#   curl -fsSL https://raw.githubusercontent.com/KeremAltaylar/Lisztmachine/master/tools/setup-mac.sh | bash
#
# It installs what building needs (Xcode's command line tools, CMake), gets the code into ~/Lisztmachine (a version:
# LISZT_VERSION=v0.12.0 before bash), installs every instrument the songs use and the visuals' media
# (tools/install-mac.sh: asks for the Mac's password once), builds the app and puts "Start Lisztmachine" on the Desktop.
# Safe to run again: it updates and keeps what is there.

say() { printf '\n==== %s\n' "$*"; }
die() { printf '\n!! %s\n' "$*"; exit 1; }
DIR="$HOME/Lisztmachine"
REPO_URL=https://github.com/KeremAltaylar/Lisztmachine.git

[ "$(uname -s)" = Darwin ] || die "This is for a Mac."
[ "$(uname -m)" = arm64 ] || echo "Note: built for Apple silicon (M1 and later); this Mac is $(uname -m)."

say "1/6 Xcode command line tools (the compiler)"
if ! xcode-select -p >/dev/null 2>&1; then
  xcode-select --install 2>/dev/null
  die "A window asks to install the command line tools: click Install, wait for it to finish, then run this command again."
fi
echo "   ready: $(xcode-select -p)"

say "2/6 CMake (the build tool)"
export PATH="$(python3 -m site --user-base)/bin:$PATH"
if ! command -v cmake >/dev/null; then python3 -m pip install --user --quiet cmake || die "Could not install CMake."; fi
cmake --version | head -1 | sed 's/^/   /'
# the same PATH in new Terminal windows, for rebuilding later
grep -q 'site --user-base' "$HOME/.zprofile" 2>/dev/null || echo 'export PATH="$(python3 -m site --user-base)/bin:$PATH"' >> "$HOME/.zprofile"

say "3/6 The code (into $DIR)"
if [ -d "$DIR/.git" ]; then git -C "$DIR" fetch --tags --quiet && git -C "$DIR" pull --quiet --ff-only || echo "   (kept the local copy as it is)"
else git clone --quiet "$REPO_URL" "$DIR" || die "Could not download the code."; fi
if [ -n "$LISZT_VERSION" ]; then git -C "$DIR" checkout --quiet "$LISZT_VERSION" || die "No version $LISZT_VERSION."; fi
echo "   at $(git -C "$DIR" describe --tags --always)"

say "4/6 Instruments, synths, Surge XT and the visuals' media (asks for your password once)"
bash "$DIR/tools/install-mac.sh" || die "The instrument install stopped: send its output to Claude."

say "5/6 Building the app (10-20 minutes the first time)"
cmake -S "$DIR" -B "$DIR/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null || die "Could not prepare the build."
cmake --build "$DIR/build" --target Lisztmachine --parallel 8 2>&1 | grep -E " error |error:|Built target Lisztmachine" | tail -5
[ -d "$DIR/build/Lisztmachine_artefacts/Release/Lisztmachine.app" ] || die "The build did not finish: send the lines above to Claude."
# each build is a new ad-hoc signature, and macOS then silently hands the app silence for its inputs (both at -120 dB,
# 2026-11-15): the old answer is forgotten so the app asks again ("Allow") when it opens
tccutil reset Microphone com.keremaltaylar.lisztmachine >/dev/null 2>&1 || true

say "6/6 A launcher on the Desktop"
L="$HOME/Desktop/Start Lisztmachine.command"
printf '#!/bin/bash\nexec bash "%s/Start Lisztmachine.command"\n' "$DIR" > "$L" && chmod +x "$L"
echo "   $L"

say "Done. Double-click \"Start Lisztmachine\" on the Desktop: the app opens, and the visuals in Chrome (install Chrome if this Mac has none)."
