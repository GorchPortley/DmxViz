# ADR 0002 – Coordinate system and units

**Status**: accepted (2026-10-02)

## Decision
* World: right-handed, **Y up**, metres, radians in memory, degrees in files and UI.
* Stage floor is `y = 0`; the audience is toward `+Z`, upstage `-Z`.
* Fixtures are modelled **hanging**: base on top, beam leaving along local `-Y`
  at pan = tilt = 0. Beam local `+Z` is the gobo/prism "up" reference.
* Pan rotates about the yoke's local Y axis, tilt about the head's local X axis.
* All in-memory colours are linear RGB.

## Rationale
Y-up matches glTF and OpenGL conventions, so models import without conversion.
GDTF (Z up, millimetres) converts with `(x, y, z)_gdtf → (x, z, -y)·0.001`.
The hanging convention matches GDTF so imported geometry trees keep their meaning.
