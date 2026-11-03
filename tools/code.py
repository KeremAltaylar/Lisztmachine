"""The composer's own source for the visuals' code layer (C): each song's spec, its form and the
function that voices its chords, cut from the C++ as it is.

    python tools/code.py            # src/core -> visuals/media/code.json
"""
import json, os, re

root = os.path.join(os.path.dirname(__file__), '..')
read = lambda p: open(os.path.join(root, p), encoding='utf-8').read().split('\n')

def block(lines, start):
    """From the line that matches start to the line that closes its first brace."""
    i = next(k for k, l in enumerate(lines) if re.search(start, l))
    depth, seen = 0, False
    for j in range(i, len(lines)):
        depth += lines[j].count('{') - lines[j].count('}')
        seen |= '{' in lines[j]
        if seen and depth <= 0: return lines[i:j + 1]
    return lines[i:]

genres, composer = read('src/core/Genres.cpp'), read('src/core/Composer.cpp')
out = {}
plan = block(composer, r'^void BerlinComposer::planForm')
out['berlin'] = dict(file='src/core/Composer.cpp', spec=plan[:20],
                     form=[l for l in plan if re.search(r'\{ "\w+",\s+\d+', l)],
                     pad=block(composer, r'^void BerlinComposer::emitPad'))
pad = block(genres, r'^void GenreComposer::emitPad')
for k, l in enumerate(genres):
    if l.startswith('const G::Spec k'):
        spec = block(genres, re.escape(l))
        scene = re.search(r'\.scene = "(\w+)"', '\n'.join(spec)).group(1)
        f = next(i for i, x in enumerate(spec) if '.form' in x)
        g = next(i for i in range(f, len(spec)) if '} } },' in spec[i])
        out[scene] = dict(file='src/core/Genres.cpp', spec=spec, form=spec[f:g + 1], pad=pad)
json.dump(out, open(os.path.join(root, 'visuals', 'media', 'code.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print(', '.join(f'{k} {len(v["spec"])}/{len(v["form"])}/{len(v["pad"])}' for k, v in out.items()))
