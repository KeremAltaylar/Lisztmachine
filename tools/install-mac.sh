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
cp "$I/doom-baritone.sfz" "$I/doom-drone.sfz" "$LIB/lib/Karoryfer_Black_And_Green_Guitars_1000/Programs/" 2>/dev/null
cp "$I/dc-erhu-trio.sfz" "$LIB/lib/aliexpress-erhu_v1000/Programs/" 2>/dev/null
cp "$I/lm-cello.sfz" "$LIB/lib/Karoryfer_Bigcat_cello.v1.001/Karoryfer_Bigcat_cello/Programs/" 2>/dev/null
cp "$I/noir-sax.sfz" "$I/wcc-bass-sax.sfz" "$LIB/lib/Weresax/Programs/" 2>/dev/null
echo "   Lisztmachine's own programs copied in"

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

say "Check: every sound the songs name"
missing=0
while IFS= read -r p; do
  rel="${p#sfz/}"
  [ -f "$LIB/lib/$rel" ] || [ -f "$LIB/$rel" ] || { echo "   MISSING sampled instrument: $rel"; missing=1; }
done < <(grep -hoE "sfz/[^#]*\.sfz" "$REPO"/sounds/*.txt | sed 's/[[:space:]]*$//' | sort -u)
while IFS= read -r n; do
  have "$n" || { echo "   MISSING plugin: $n.vst3"; missing=1; }
done < <(grep -hoE "vst/[^/]+" "$REPO"/sounds/*.txt | sed 's|vst/||' | sort -u)
[ -d "$REPO/plugins/Dexed.vst3" ] || [ -d "/Library/Audio/Plug-Ins/VST3/Dexed.vst3" ] || { echo "   MISSING plugin: Dexed.vst3"; missing=1; }
if [ $missing -eq 0 ] && [ $fail -eq 0 ]; then echo "   ALL INSTRUMENTS FOUND"; else echo "   Some are missing: send this output to Claude."; fi
