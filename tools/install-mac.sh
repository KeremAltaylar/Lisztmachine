#!/bin/bash
# Installs on a Mac every instrument the Lisztmachine songs use, as on the PC (2026-11-14):
#   sampled instruments (free, mostly CC0) into ~/liszt-instruments, with this repo's own programs copied in;
#   the plugin synths (Mac versions, free) into the VST3 folders; Dexed into ~/Lisztmachine/plugins.
# Run from the repo:  bash tools/install-mac.sh      (asks for your Mac password once, for the .pkg installers)
# Safe to run again: what is already there is kept. At the end it checks every sound the palettes name.

REPO="$(cd "$(dirname "$0")/.." && pwd)"
LIB="$HOME/liszt-instruments"
DL="$HOME/Downloads/lisztmachine-install"
VST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$LIB/lib" "$DL" "$VST" "$REPO/plugins"
fail=0
say() { printf '\n== %s\n' "$*"; }
get() { # url file: download once
  [ -s "$DL/$2" ] && return 0
  curl -fL --retry 3 -o "$DL/$2.part" "$1" && mv "$DL/$2.part" "$DL/$2" || { echo "   could not download $1"; fail=1; return 1; }
}

say "Sampled instruments (into $LIB)"
lib() { # url zip target-folder: unzip into lib/<target> unless it is there
  local url="$1" zip="$2" dir="$3"
  if [ -d "$LIB/lib/$dir" ]; then echo "   $dir: already there"; return; fi
  get "$url" "$zip" || return
  if [ "$dir" = "Weresax" ]; then ditto -x -k "$DL/$zip" "$LIB/lib"; else mkdir -p "$LIB/lib/$dir" && ditto -x -k "$DL/$zip" "$LIB/lib/$dir"; fi
  echo "   $dir: installed"
}
G=https://github.com/sfzinstruments
lib $G/dsmolken.double-bass/releases/download/v1.001/DSmolken.double_bass.v1.001.zip DSmolken.double_bass.v1.001.zip DSmolken.double_bass.v1.001
lib $G/karoryfer.weresax/releases/download/v1.003/Karoryfer.Weresax.v.1.003.zip Karoryfer.Weresax.v.1.003.zip Weresax
lib $G/karoryfer.growlybass/releases/download/v1.002/Karoryfer.Growlybass.v1.002.zip Karoryfer.Growlybass.v1.002.zip Karoryfer.Growlybass.v1.002
lib $G/karoryfer.scarypiano/releases/download/v1.002/Karoryfer.Scarypiano.v1.002.zip Karoryfer.Scarypiano.v1.002.zip Karoryfer.Scarypiano.v1.002
lib $G/karoryfer-bigcat.cello/releases/download/v1.001/Karoryfer_Bigcat_cello.v1.001.zip Karoryfer_Bigcat_cello.v1.001.zip Karoryfer_Bigcat_cello.v1.001
lib $G/karoryfer.black-and-green-guitars/releases/download/v1.000/Karoryfer_Black_And_Green_Guitars_1000.zip Karoryfer_Black_And_Green_Guitars_1000.zip Karoryfer_Black_And_Green_Guitars_1000
lib $G/aliexpress-erhu/releases/download/v1.000/aliexpress-erhu_v1000.zip aliexpress-erhu_v1000.zip aliexpress-erhu_v1000
if [ ! -d "$LIB/steel-drum" ]; then git clone --depth 1 $G/jlearman.SteelDrum "$LIB/steel-drum" && echo "   steel-drum: installed" || fail=1; else echo "   steel-drum: already there"; fi
if [ ! -d "$LIB/vsco2/Brass" ]; then   # VSCO 2 CE, only the five instruments the songs play
  rm -rf "$LIB/vsco2"
  git clone --filter=blob:none --sparse https://github.com/sgossner/VSCO-2-CE "$LIB/vsco2" &&
  git -C "$LIB/vsco2" sparse-checkout set "Brass/F Horn" "Brass/Tenor Trombone" "Brass/Tuba" "Woodwinds/Bassoon" "Woodwinds/Clarinet" &&
  echo "   vsco2: installed" || fail=1
else echo "   vsco2: already there"; fi
# this repo's own programs (tools/instruments/README.md says where each goes)
I="$REPO/tools/instruments"
cp "$I"/Bassoon-*.sfz "$I"/Clarinet-*.sfz "$I"/FHorn-*.sfz "$I"/TenorTrombone-*.sfz "$I"/Tuba-*.sfz "$LIB/vsco2/" 2>/dev/null
cp "$I/doom-baritone.sfz" "$I/doom-drone.sfz" "$I/wake-baritone.sfz" "$I/wake-drone.sfz" "$I/saint-drone.sfz" "$LIB/lib/Karoryfer_Black_And_Green_Guitars_1000/Programs/" 2>/dev/null
cp "$I/dc-erhu-trio.sfz" "$LIB/lib/aliexpress-erhu_v1000/Programs/" 2>/dev/null
cp "$I/lm-cello.sfz" "$LIB/lib/Karoryfer_Bigcat_cello.v1.001/Karoryfer_Bigcat_cello/Programs/" 2>/dev/null
cp "$I/noir-sax.sfz" "$I/wcc-bass-sax.sfz" "$LIB/lib/Weresax/Programs/" 2>/dev/null
echo "   Lisztmachine's own programs copied in"
# Bleach's breaks (2026-11-21): Kerem's KAN Amen Break Tribute pack, from his WeTransfer link (its licence: not to be
# shared, so it is not in the repo), with the repo's list of which breaks to play (bleach.breaks) beside its loops
K="$LIB/lib/KAN Amen Break Tribute"
if [ ! -d "$K/Drum Loops" ]; then
  kan=$(curl -s -X POST "https://wetransfer.com/api/v4/transfers/966d7c4037ea1525f106f68ea5b9780a20240918101535/download"         -H "Content-Type: application/json" -H "x-requested-with: XMLHttpRequest" -d '{"security_hash":"fde53a","intent":"entire_transfer"}' |
        python3 -c "import sys,json; print(json.load(sys.stdin)['direct_link'])" 2>/dev/null)
  if [ -n "$kan" ] && get "$kan" kan-amen.zip && unzip -q -o "$DL/kan-amen.zip" -x "__MACOSX/*" -d "$LIB/lib"; then echo "   KAN Amen Break Tribute: installed"
  else echo "   KAN Amen Break Tribute: could not download (the WeTransfer link may have expired)"; fail=1; fi
fi
cp "$I/bleach.breaks" "$K/Drum Loops/" 2>/dev/null
# Improvisation's wider pool (2026-11-18, ~11 GB): tools/extra-instruments.txt, laid out as on the PC: a zip with one top
# folder unpacked into lib/ (that folder), any other into lib/<zip name>
grep -v '^#' "$REPO/tools/extra-instruments.txt" | tr -d '\r' | while read -r url; do
  [ -n "$url" ] || continue
  zip="$(basename "$url")"; [ "$zip" = 1.1.0.zip ] && zip=VSCO-2-CE-1.1.0.zip
  done_mark="$LIB/lib/.done-$zip"
  [ -f "$done_mark" ] && continue
  get "$url" "$zip" || continue
  if [ "$(unzip -Z1 "$DL/$zip" | cut -d/ -f1 | sort -u | wc -l)" -eq 1 ]; then unzip -q -n "$DL/$zip" -d "$LIB/lib"
  else mkdir -p "$LIB/lib/${zip%.zip}" && unzip -q -n "$DL/$zip" -d "$LIB/lib/${zip%.zip}"; fi && touch "$done_mark" && rm -f "$DL/$zip" && echo "   ${zip%.zip}: installed"
done

say "Plugin synths (Mac versions)"
have() { find "$VST" "/Library/Audio/Plug-Ins/VST3" -maxdepth 2 -iname "$1.vst3" 2>/dev/null | grep -q .; }
install_from() { # a downloaded file: a .pkg is installed, a .dmg or .zip is opened and its .pkg installed or .vst3 copied
  local f="$DL/$1" tmp
  case "$f" in
    *.pkg) sudo installer -pkg "$f" -target / >/dev/null && return 0 ;;
    *.dmg) tmp=$(mktemp -d); hdiutil attach -nobrowse -quiet -mountpoint "$tmp" "$f" || return 1 ;;
    *.zip) tmp=$(mktemp -d); ditto -x -k "$f" "$tmp" || return 1 ;;
  esac
  local ok=1 p v
  p=$(find "$tmp" -maxdepth 4 -name "*.pkg" | head -1)
  if [ -n "$p" ]; then sudo installer -pkg "$p" -target / >/dev/null && ok=0
  else
    while IFS= read -r v; do cp -R "$v" "$VST/" && ok=0; done < <(find "$tmp" -maxdepth 5 -name "*.vst3" -type d -prune)
  fi
  case "$f" in *.dmg) hdiutil detach -quiet "$tmp" ;; esac
  return $ok
}
plugin() { # name url file
  if have "$1"; then echo "   $1: already there"; return; fi
  get "$2" "$3" && install_from "$3" && echo "   $1: installed" || { echo "   $1: could not install ($3)"; fail=1; }
}
# Surge XT itself (the engine's main synth and its delay/reverb buses), when this Mac has none
plugin "Surge XT" https://github.com/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-macOS-1.3.4.dmg surge-xt-macOS-1.3.4.dmg
FB=https://www.fullbucket.de/music/dl
plugin fb3300   $FB/fb3300_1_2_5_mac.pkg   fb3300_1_2_5_mac.pkg
plugin fb7999   $FB/fb7999_1_1_7_mac.pkg   fb7999_1_1_7_mac.pkg
plugin deputy   $FB/deputy_1_4_0_mac.pkg   deputy_1_4_0_mac.pkg
plugin scrooo   $FB/scrooo_2_0_4_mac.pkg   scrooo_2_0_4_mac.pkg
plugin monofury $FB/monofury_2_3_2_mac.pkg monofury_2_3_2_mac.pkg
plugin TAL-NoiseMaker https://tal-software.com/downloads/plugins/TAL-NoiseMaker-installer_macos.zip TAL-NoiseMaker-installer_macos.zip
plugin OB-Xf      https://github.com/surge-synthesizer/OB-Xf/releases/download/v1.0.3/ob-xf-macOS-v1.0.3.dmg ob-xf-macOS-v1.0.3.dmg
plugin "Six Sines" https://github.com/baconpaul/six-sines/releases/download/v1.2.1/six-sines-macOS-2026-07-20-18ecb36.dmg six-sines-macOS-v1.2.1.dmg
plugin JC303      https://github.com/midilab/jc303/releases/download/v0.14.0/jc303-0.14.0-macos_universal-plugins.zip jc303-0.14.0-macos.zip
plugin RipplerX   https://github.com/tiagolr/ripplerx/releases/download/v1.5.19/ripplerx-macos-v1.5.19.zip ripplerx-macos-v1.5.19.zip
plugin js80p      https://github.com/attilammagyar/js80p/releases/download/v4.1.1/js80p-4_1_1-macos-universal-vst3_bundle-signed.pkg js80p-4_1_1-macos-vst3.pkg
if [ ! -d "$REPO/plugins/Dexed.vst3" ] && [ ! -d "/Library/Audio/Plug-Ins/VST3/Dexed.vst3" ]; then
  get https://github.com/asb2m10/dexed/releases/download/v1.0.1/Dexed-1.0.1-macOS.zip Dexed-1.0.1-macOS.zip &&
  tmp=$(mktemp -d) && ditto -x -k "$DL/Dexed-1.0.1-macOS.zip" "$tmp" &&
  v=$(find "$tmp" -maxdepth 4 -name Dexed.vst3 | head -1) && [ -n "$v" ] && cp -R "$v" "$REPO/plugins/" && echo "   Dexed: installed" || { echo "   Dexed: could not install"; fail=1; }
else echo "   Dexed: already there"; fi

say "Visuals media (the code shown in the picture, the photographs, the songs' recordings)"
# not in the repository (too large): from the newest GitHub release's visuals zip, into visuals/media
if [ ! -f "$REPO/visuals/media/code.json" ]; then
  url=$(curl -fsL https://api.github.com/repos/KeremAltaylar/Lisztmachine/releases/latest | grep -o '"browser_download_url": *"[^"]*visuals[^"]*\.zip"' | head -1 | sed 's/.*"\(http[^"]*\)"/\1/')
  if [ -n "$url" ] && get "$url" visuals-media.zip; then
    tmp=$(mktemp -d) && ditto -x -k "$DL/visuals-media.zip" "$tmp" &&
    mkdir -p "$REPO/visuals/media" && cp -Rn "$tmp"/visuals/media/ "$REPO/visuals/media/" && echo "   visuals media: installed" || { echo "   visuals media: could not unpack"; fail=1; }
  else echo "   visuals media: could not download"; fail=1; fi
else echo "   visuals media: already there"; fi
# a song's photographs the release zip predates (2026-11-20: the three new songs): each one fetched from Wikimedia
# Commons by its name in visuals/image-sources.json, resized there to 2048 px, one at a time (Commons limits the rate)
python3 - "$REPO" <<'PY' || fail=1
import json, os, subprocess, sys, time, urllib.parse
repo = sys.argv[1]
songs = json.load(open(os.path.join(repo, "visuals", "image-sources.json")))["songs"]
bad = 0
for song, titles in songs.items():
    for n, title in enumerate(titles, 1):
        out = os.path.join(repo, "visuals", "media", "img", song, f"{n}.jpg")
        if os.path.exists(out) and os.path.getsize(out) > 0:
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        url = "https://commons.wikimedia.org/wiki/Special:FilePath/" + urllib.parse.quote(title) + "?width=2048"
        ok = subprocess.run(["curl", "-fsL", "--retry", "3", "-A", "LisztmachineInstall/1.0 (personal art project)", "-o", out + ".part", url]).returncode == 0
        if ok: os.replace(out + ".part", out); print(f"   photo {song}/{n}: installed")
        else: bad = 1; print(f"   photo {song}/{n}: could not download {title}")
        time.sleep(1.5)
sys.exit(bad)
PY

say "Check: every sound the songs name"
missing=0
while IFS= read -r p; do
  rel="${p#sfz/}"
  [ -f "$LIB/lib/$rel" ] || [ -f "$LIB/$rel" ] || { echo "   MISSING sampled instrument: $rel"; missing=1; }
done < <(grep -hoE "sfz/[^#]*\.(sfz|breaks)" "$REPO"/sounds/*.txt | sed 's/[[:space:]]*$//' | sort -u)
while IFS= read -r n; do
  have "$n" || { echo "   MISSING plugin: $n.vst3"; missing=1; }
done < <(grep -hoE "vst/[^/]+" "$REPO"/sounds/*.txt | sed 's|vst/||' | sort -u)
[ -d "$REPO/plugins/Dexed.vst3" ] || [ -d "/Library/Audio/Plug-Ins/VST3/Dexed.vst3" ] || { echo "   MISSING plugin: Dexed.vst3"; missing=1; }
# Surge's own sounds: a Surge release can rename or drop a patch (the app then skips it; listed here to replace)
SURGE="/Library/Application Support/Surge XT"
while IFS= read -r patch; do
  case "$patch" in
    3rd/*) f="$SURGE/patches_3rdparty/${patch#3rd/}.fxp" ;;
    *) f="$SURGE/patches_factory/$patch.fxp" ;;
  esac
  carried="$REPO/sounds/surge-patches/${f#$SURGE/}"   # the songs carry the ones some Surge releases lack
  [ -f "$f" ] || [ -f "$carried" ] || { echo "   MISSING Surge patch: $patch"; missing=1; }
done < <(cat "$REPO"/sounds/*.txt | tr -d '\r' | sed 's/#.*//' | awk 'NF >= 4 && $1 !~ /^(gain|delay|drive|crush|comp|duck|lowcut|amp|release|feed|send|return|feedback|shimmer|master)$/ {
           p = $0; sub(/^[ \t]*[^ \t]+[ \t]+[^ \t]+[ \t]+[^ \t]+[ \t]+/, "", p); sub(/[ \t]+$/, "", p); print p }' |
         grep -vE '^(internal|sfz|vst|dexed|sample)/' | sort -u)
if [ $missing -eq 0 ] && [ $fail -eq 0 ]; then echo "   ALL INSTRUMENTS FOUND"; else echo "   Some are missing: send this output to Claude."; fi
