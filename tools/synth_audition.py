# Renders four sounds of every new synth playing the same short dark phrase (liszt_render --demo) and
# writes them, with a manifest, into synths/ for the Synth Sounds page, where Kerem picks the ones he
# likes. Usage: python tools/synth_audition.py [synth name ...]   (no names: all of them)
import json, os, subprocess, sys, soundfile as sf

RENDER = r"C:/Users/kerem/build/Lisztmachine/liszt_render_artefacts/Release/liszt_render.exe"
OUT = "synths"
TMP = os.environ.get("TEMP", ".") + "/liszt-synth-demo.wav"

# synth: (what it is, [(label, patch after "vst/<plugin>/"), ...]); the plugin is the .vst3 file name
SYNTHS = {
    "MoniqueMonosynth": ("Monique: a morphing mono synth", [
        ("Overdriven Drone", "Yemski Overdriven Drone"), ("Hairy Growler", "Yemski Hairy Growler"),
        ("A Dark One", "A DARK ONE"), ("Lost in FM Space", "LOST IN FM SPACE 1")]),
    "Resonarium": ("Resonarium: coupled strings and resonators", [
        ("Colour Out of Space", "Colour Out of Space"), ("Netherstorm", "Netherstorm"),
        ("Waterphone", "Waterphone"), ("Garden of Unearthly Delights", "Garden of Unearthly Delights")]),
    "RipplerX": ("RipplerX: struck and bowed resonators", [
        ("Gong", "Gong"), ("Tubes", "Tubes"), ("Bells", "Bells2"), ("Strings", "Strings")]),
    "Tunefish4": ("Tunefish 4: additive pads and basses", [
        ("Brainsaw", "Brainsaw"), ("Distant Strings", "Distant Strings"), ("Lions Roar", "Lions Roar"), ("Fuzzystring", "Fuzzystring")]),
    "deputy": ("The Deputy: a 70s string machine", [
        ("Carrying No Cross", "Pad: Carrying No Cross"), ("Basso Oooh", "Poly: Basso Oooh"),
        ("Attack of the Drone", "SFX: Attack of the Drone"), ("No Safe Ground", "Pad: No Safe Ground")]),
    "fb3300": ("FB-3300: the Korg PS-3300", [
        ("Multiphonic", "PAD: Multiphonic"), ("Schoenerism", "PAD: Schoenerism"), ("Recto Rezo", "KEY: Recto Rezo"), ("Subbass", "BAS: Subbass")]),
    "fb7999": ("FB-7999: the Korg DW-8000", [
        ("Sleeping Giant", "SLEEPING GIANT"), ("Cathedral Organ", "CATHEDRAL ORGAN 1"), ("Low Strings", "LOW STRINGS"), ("Disintegrator", "DISINTEGRATOR")]),
    "js80p": ("JS80P: wavefolder and FM", [
        ("Dystopian Cathedral", "Dystopian Cathedral"), ("Nightmare", "Nightmare"), ("Creepy Wind", "Creepy Wind"), ("Gloomy Brass", "Gloomy Brass")]),
    "monofury": ("Mono/Fury: the Korg Mono/Poly", [
        ("Spooky Unison", "Spooky Unison Share"), ("The Bad Ass", "The Bad Ass"), ("Dead Elevator", "Dead Elevator"), ("Alien Subversion", "Alien Subversion")]),
    "scrooo": ("scrooo: formant synthesis", [
        ("Tomb of Frankenstein", "Pad: Tomb Of Frankenstein"), ("Scary Choir", "Pad: Scary Choir"),
        ("Raizer Drone", "Drone: Raizer"), ("Wraiths of Industry", "Pad: Wraiths of Industry")]),
    "Aeolus": ("Aeolus: a pipe organ", [("Registration 1", "#0"), ("Registration 2", "#1"), ("Registration 3", "#2"), ("Registration 4", "#3")]),
    "firefly_synth_1": ("Firefly: FM and distortion", [
        ("Infernal Acid", "|Master Preset=Infernal Acid (Aux1+2)"), ("Distorted Bells", "|Master Preset=Distorted Bells (Aux1+2)"),
        ("FM Weirdness", "|Master Preset=FM Weirdness"), ("Big Pad", "|Master Preset=Big Pad 3 (Aux 1+2+3+4+5)")]),
    # no presets: settings made here, by parameter
    "JC303": ("JC303: a 303 with modelled overdrive", [
        ("Dark acid", "|Cutoff=0.25|Resonance=0.85|EnvMod=0.5|Decay=0.5|Switch Overdrive Mod=1|Drive=0.5"),
        ("Squelch", "|Cutoff=0.4|Resonance=0.95|EnvMod=0.8|Decay=0.3|Switch Overdrive Mod=1|Drive=0.3"),
        ("Square growl", "|Waveform=0|Cutoff=0.2|Resonance=0.6|EnvMod=0.3|Decay=0.8|Switch Overdrive Mod=1|Drive=0.8"),
        ("Deep and clean", "|Cutoff=0.15|Resonance=0.7|EnvMod=0.2|Decay=0.9|Drive=0")]),
    "TAL-NoiseMaker": ("TAL-NoiseMaker: a virtual analog", [
        ("Dark pad", "|Filter Cutoff=0.35|Filter Resonance=0.2|Amp Attack=0.45|Amp Release=0.55|Osc 2 Volume=0.8|Osc 2 Fine Tune=0.53|Chorus 1 Enable=1|Reverb Wet=0.35|Reverb Decay=0.8"),
        ("Fat bass", "|Filter Cutoff=0.3|Filter Resonance=0.35|Filter Contour=0.7|Filter Decay=0.35|Filter Sustain=0.2|Osc 2 Volume=0.8|Osc 2 Fine Tune=0.52|Filter Drive=0.6"),
        ("Crushed lead", "|Filter Cutoff=0.55|Filter Resonance=0.5|Osc Bitcrusher=0.35|Delay Wet=0.3|Amp Release=0.3"),
        ("Detuned drone", "|Master Detune=0.6|Osc 2 Volume=0.8|Filter Cutoff=0.45|Amp Attack=0.6|Amp Release=0.7|Reverb Wet=0.5|Reverb Decay=0.9|Filter Drive=0.4")]),
    "OB-Xf": ("OB-Xf: the Oberheim OB-X", [
        ("Brass pad", "|Osc 2 Volume=0.8|Osc 2 Detune=0.15|Filter Cutoff=0.45|Filter Resonance=0.2|Filter Env Amount=0.4|Filter Env Attack=0.4|Filter Env Decay=0.6|Filter Env Sustain=0.5|Amp Env Attack=0.35"),
        ("Ring metal", "|Osc 2 Volume=0.6|Ring Mod Volume=0.7|Osc 2 Pitch=0.6|Filter Cutoff=0.5|Filter Resonance=0.4|Filter 4-Pole Mode=1"),
        ("4-pole bass", "|Filter 4-Pole Mode=1|Filter Cutoff=0.3|Filter Resonance=0.5|Filter Env Amount=0.6|Filter Env Decay=0.4|Filter Env Sustain=0.1|Osc 2 Volume=0.8|Osc 2 Detune=0.1"),
        ("Xpander drone", "|Filter 4-Pole Xpander=1|Filter Xpander Mode=0.5|Filter Cutoff=0.4|Filter Resonance=0.6|Osc 2 Volume=0.8|Osc 2 Detune=0.3|Noise Volume=0.2|Amp Env Attack=0.5")]),
    "Six Sines": ("Six Sines: six-operator FM", [
        ("Feedback sine", "|Op 1 Feedback Active=1|Op 1 Feedback Level=0.7"),
        ("Hard feedback", "|Op 1 Feedback Active=1|Op 1 Feedback Level=0.92|Op 1 Feedback Overdrive=1"),
        ("Saturated", "|Op 1 Feedback Active=1|Op 1 Feedback Level=0.8|Main Saturation Type=0.5|Main Saturation Drive=0.6"),
        ("Bit-crushed", "|Op 1 Feedback Active=1|Op 1 Feedback Level=0.75|Main Bit Depth Adjust=0.6|Main Env Release=0.7")]),
}

def main(only):
    os.makedirs(f"{OUT}/media", exist_ok=True)
    man_path = f"{OUT}/manifest.json"
    man = json.load(open(man_path)) if os.path.exists(man_path) else {"synths": []}
    by_name = {s["plugin"]: s for s in man["synths"]}
    for plugin, (about, sounds) in SYNTHS.items():
        if only and plugin not in only: continue
        entry = {"plugin": plugin, "about": about, "sounds": []}
        for k, (label, rest) in enumerate(sounds):
            patch = f"vst/{plugin}/{rest}"
            r = subprocess.run([RENDER, "--demo", patch, TMP], capture_output=True, text=True, timeout=300)
            ok = r.returncode == 0 and os.path.exists(TMP)
            fid = f"{plugin.replace(' ', '_')}_{k}"
            if ok:
                x, sr = sf.read(TMP)
                sf.write(f"{OUT}/media/{fid}.mp3", x, sr, format="MP3", compression_level=0.5)
                os.remove(TMP)
            print(f"{'ok  ' if ok else 'FAIL'} {patch}  {(r.stderr or r.stdout).strip().splitlines()[-1] if (r.stderr or r.stdout).strip() else ''}", flush=True)
            entry["sounds"].append({"id": fid, "label": label, "patch": patch, "ok": ok})
        by_name[plugin] = entry
    man["synths"] = list(by_name.values())
    json.dump(man, open(man_path, "w"), indent=1)

if __name__ == "__main__":
    main(set(sys.argv[1:]))
