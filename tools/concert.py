"""The whole concert as one show for the visuals: the songs' show data back to back.

    python tools/concert.py            # visuals/media/<song>1.show.json -> visuals/media/concert1.show.json

The audio stays one file per song (one 46-minute file is too big to publish and to decode);
the visuals page plays them back to back from the 'songs' list.
"""
import json, os

ORDER = [('berlin', 'seed1'), ('stoner', 'stoner1'), ('jungle', 'jungle1'), ('ecstatic', 'ecstatic1'), ('noir', 'noir1'),
         ('deconstructed', 'deconstructed1'), ('halftime', 'halftime1'), ('doom', 'doom1'), ('blackgaze', 'blackgaze1')]
media = os.path.join(os.path.dirname(__file__), '..', 'visuals', 'media')

out = dict(seed=1, notes=[], levels=[], sections=[], songs=[])
t0 = 0.0
for piece, name in ORDER:
    d = json.load(open(os.path.join(media, name + '.show.json'), encoding='utf-8'))
    if not out['songs']:
        out.update(bpm=d['bpm'], tracks=d['tracks'], levelNames=d['levelNames'], levelRate=d['levelRate'])
    assert d['levelRate'] == out['levelRate'] and d['tracks'] == out['tracks'], name
    out['songs'].append(dict(piece=piece, media='media/' + name, t=round(t0, 4), bpm=d['bpm'], key=d['key']))
    out['notes'] += [[round(n[0] + t0, 4)] + n[1:] for n in d['notes']]
    out['sections'] += [dict(s, t=round(s['t'] + t0, 4), song=len(out['songs']) - 1) for s in d['sections']]
    out['levels'] += d['levels']
    t0 = len(out['levels']) / out['levelRate']   # the next song starts where this recording ends
out['key'] = '%d songs · %d:%02d' % (len(ORDER), *divmod(round(t0), 60))
json.dump(out, open(os.path.join(media, 'concert1.show.json'), 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
print(out['key'])
