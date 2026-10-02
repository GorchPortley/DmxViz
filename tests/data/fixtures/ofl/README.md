# Open Fixture Library samples

Fixture definitions copied unmodified from the
[Open Fixture Library](https://github.com/OpenLightingProject/open-fixture-library)
(`fixtures/<manufacturer>/<fixture>.json`, commit `1424c7a4bbc103b32596f8c5104d7bcf46be8465`,
2026-10-02). They are licensed under the MIT licence, see [LICENSE](LICENSE);
copyright Florian & Felix Edelmann and the OFL contributors named in each file's `meta.authors`.

| File | Why it is here |
|------|----------------|
| `robe/robin-600e-spot.json` | spot: CMY, colour wheel, static + rotating gobo wheels, switching channels, prism, iris, frost, zoom/focus, 16-bit channels |
| `showtec/phantom-50-led-spot.json` | spot: gobo wheel with `resource` references, gobo rotation, prism |
| `clay-paky/sharpy.json` | beam moving head: 0–3.8° lens, colour + gobo wheel, prism rotation by angle |
| `martin/mac-viper-wash.json` | wash with zoom, CMY + CTO, iris, framing barn doors |
| `showtec/club-par-12-4-rgbw.json` | RGBW LED par |
| `showtec/pixel-bar-12-mkii.json` | 12-pixel bar: `matrix`, `templateChannels`, pixel groups (halves, thirds, ...) |
| `martin/atomic-3000.json` | strobe: rate / duration / effect channels |

The gobo images these fixtures reference live in the OFL `resources/gobos/`
folder and come from QLC+ (Apache-2.0); they are deliberately **not** copied.
The tests write their own placeholder images into a temporary OFL-style tree.
