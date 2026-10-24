"""Render an audition pack: the same bars played by many patches, one part at a time,
loudness-matched so the louder clip does not win. Kerem picks by number.

    python tools/audition_pack.py <out dir>
"""
import os
import subprocess
import sys
import zipfile

import numpy as np
import soundfile as sf

RENDER = r'C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe'
SEED, SECTION, BARS = 1, 'Summit', 8
TARGET_RMS_DB = -20.0

CANDIDATES = {
    'bass': ['Basses/Behemoth', 'Basses/Doomsday', 'Basses/Evilous', 'Basses/Dist Bass 1', 'Basses/Dist Bass 2',
             'Basses/Distorted Bass', 'Basses/Distorted FM', 'Basses/Distorted MW', 'Basses/Lord Sawtooth',
             'Basses/Saw Lo-Fi', 'Basses/Static 1', 'Basses/Metal Square'],
    'lead': ['Leads/Distortionworks', 'Leads/Harsh', 'Leads/Nastyfication', 'Leads/Scream Lead', 'Leads/Untamed',
             'Leads/Turbo', 'Leads/Turbo Solo', 'Leads/Rough', 'Leads/Synth Guitar 1', 'Leads/Synth Guitar 2',
             'Leads/Squelch', 'Leads/Resofest 2'],
    'seq': ['Sequences/Distorted Glassy Seq', 'Sequences/Evilish', 'Sequences/Acid Seq 1', 'Sequences/Acid Seq 2',
            'Sequences/Acid Seq 3', 'Sequences/Madness', 'Sequences/Sync Accident', 'Sequences/Fold Sseq',
            'Sequences/FB Seq', 'Sequences/Damn Aliens', 'Sequences/Burial Ground', 'Sequences/Tyskland'],
    'pad': ['Polysynths/Serious Distortion', 'Polysynths/Hugeness', 'Polysynths/Megasynth 1', 'Polysynths/Megasynth 3',
            'Polysynths/Havoc', 'Polysynths/Dirty Hole', 'Polysynths/Disturbing Resonance', 'Polysynths/Uni Saw FB',
            'Polysynths/Zizzly Saw', 'Pads/Smooth Dist', 'Pads/Harsh Saw', 'Pads/Endgame'],
}


def render(out_wav, extra):
    cmd = [RENDER, '--seed', str(SEED), '--from', SECTION, '--bars', str(BARS), '--out', out_wav] + extra
    subprocess.run(cmd, check=True, capture_output=True, cwd=os.path.dirname(out_wav))


def to_mp3(wav, mp3):
    x, sr = sf.read(wav)
    rms = np.sqrt((x ** 2).mean())
    if rms < 1e-6:
        return None
    gain = 10 ** (TARGET_RMS_DB / 20) / rms
    gain = min(gain, 0.97 / np.abs(x).max())  # never clip; such a clip ends up a little quieter
    sf.write(mp3, x * gain, sr, format='MP3', compression_level=0.1)
    for ext in ('.wav', '.mid', '.sections.txt'):
        p = wav[:-4] + ext
        if os.path.exists(p):
            os.remove(p)
    return 20 * np.log10(rms)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    lines = ['Lisztmachine audition pack, round 1: character',
             f'Every clip: seed {SEED}, the first {BARS} bars of "{SECTION}", that part alone, through the shared',
             'delay and reverb. Loudness-matched.',
             '', '00-reference: the whole mix as it is now.', '']
    wav = os.path.join(out_dir, '00-reference.wav')
    render(wav, [])
    to_mp3(wav, wav[:-4] + '.mp3')
    for part, patches in CANDIDATES.items():
        lines.append(part.upper())
        for i, patch in enumerate(patches, 1):
            name = f'{part}-{i:02d} {patch.split("/")[1]}'
            wav = os.path.join(out_dir, name + '.wav')
            render(wav, ['--solo', part, '--patch', f'{part}={patch}'])
            level = to_mp3(wav, os.path.join(out_dir, name + '.mp3'))
            note = '' if level is not None else '  (SILENT: no clip)'
            lines.append(f'  {i:2d}  {patch}{note}')
            print(name, 'silent' if level is None else f'{level:.1f} dBFS raw', flush=True)
        lines.append('')
    lines.append('Reply with e.g.: bass 3, 9 / lead 1 / seq 2, 5 / pad none')
    with open(os.path.join(out_dir, 'README.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')

    zpath = out_dir.rstrip('\\/') + '.zip'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_STORED) as z:
        for fn in sorted(os.listdir(out_dir)):
            z.write(os.path.join(out_dir, fn), os.path.join(os.path.basename(out_dir), fn))
    print('pack:', zpath, f'{os.path.getsize(zpath) / 1e6:.1f} MB')


if __name__ == '__main__':
    main(sys.argv[1])
