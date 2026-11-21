# Sampled instruments

Played by sfizz (built into the engine). Palette lines `sfz/<path>` resolve under
`~/liszt-instruments/lib`, `~/liszt-instruments` or `~/Music/Lisztmachine/instruments`. A missing
instrument is skipped (the track plays its other sounds).

Installed 2026-10-29 (all CC0 unless noted), from github.com/sfzinstruments release zips into `lib/`:
DSmolken double bass, Karoryfer Bigcat cello, Black And Green Guitars, Growlybass, Scarypiano,
Bear Sax, aliexpress erhu; cloned: jlearman SteelDrum (Unlicense) as `steel-drum/`, body_percussion
as `body-percussion/`; VSCO 2 CE (sgossner/VSCO-2-CE, sparse: Woodwinds/Bassoon, Clarinet, Brass/F Horn,
Tenor Trombone, Tuba) as `vsco2/`, with the generated mappings in this folder (copy them into `vsco2/`).
doom-baritone.sfz: a wrapper of Black And Green Guitars 10-combo_twang an octave down (copy it into `lib/Karoryfer_Black_And_Green_Guitars_1000/Programs/`).
dc-erhu-trio.sfz: a wrapper of the erhu 05-erhu_sul_tasto with the trio on and a deep, unsteady vibrato (copy it into `lib/aliexpress-erhu_v1000/Programs/`).
lm-cello.sfz: Bigcat cello "01- Bowed (velocity layer)" with note_offset=12, because its samples are mapped an octave low (C1_p = the open C string at 65 Hz sits on key 24) and so sounded an octave high; sfizz applies note_offset with the opposite sign (copy it into `lib/Karoryfer_Bigcat_cello.v1.001/Karoryfer_Bigcat_cello/Programs/`).

- `noir-sax.sfz` (Weresax/Programs): Sax.sfz with its vibrato on (cc111 55, unsteady 40, uneven pitch 20), for the Noir solo.
- `wcc-bass-sax.sfz` (Weresax/Programs): Sax.sfz an octave down (note_offset=-12 plus transpose=-12: each key plays the sample an octave above, pitched down), no vibrato: the low saxophone of Working Class Curse. Measured pitch-true from E2 (40) to E4 (64), beating 2-8 %.
- `wake-baritone.sfz` (black guitar alone; its amp feedback up, faster and louder, so each hit sustains), `wake-drone.sfz` (black guitar alone), `saint-drone.sfz` (green guitar alone) (Black And Green Guitars Programs, 2026-11-21): Hollow Wake, in Doom's style, its own guitars. Pitch-true within 10 cents.
