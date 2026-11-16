# Golden haze: a long view and a wall of light toward the sun

The target is a clear evening that is still hazy: you see kilometres to the side,
but looking into a low sun the air glows. Tree lines pale and warm in layers with
distance, shafts fall through the near trees and backlit leaves glow at their edges.
Not every day: on some clear days the air is crisp.

The first tuning pass is in `lighting.json` and `picture.json`: a stronger
`sun_glow_at_sun`, warmer 0° and 10° keys, mist 50 m deep with the sun 10° up
(8 m at the horizon, so the dawn mist still lies low below the treetops), a wider
sun halo and a lower `bloom_threshold`. Judge it in the visual tour's sunset stops
before building the steps below.

## 1. Daytime haze from visibility

Today `haze_over` hazes by `haze_amount * (distance / full_distance)^2`, full at
the course's `haze_distance` capped to the play area (`src/renderer/scene_haze.h`).
`clear_visibility_meters` (Koschmieder) feeds only the night lights' beams. So
view distance and sun glow are one knob, and the near air holds almost no haze,
which keeps sun shafts faint in clear air.

- Make `haze_over`'s distance term real extinction, `1 - exp(-sigma * d)`, with
  sigma = 3.912 / visibility from `scattering_under_weather` (one number for the
  day haze and the beams). Keep the low mist's closed form.
- The land panorama hazes by its own distance (`sky_distance`) through the same
  law. Remove the course `haze_distance`/`haze_amount` keys, or keep them only for
  what the panorama has painted in; remove `make_scene_haze`'s area-extent cap.
- With visibility near 10-15 km and forward scattering (g about 0.6-0.7), the
  air stays see-through sideways and glows about 20x brighter toward the sun.
  The shaft pass then has near air to take its shadows out of, unchanged.
- Update the CPU copy (`src/renderer/scattered_light.h`), `tests/shader_tests.cpp`,
  the haze paragraph in `docs/rendering_rules.md` and the code map line.

## 2. Hazy and crisp days in the weather

- Add `haze` (0 crisp .. 1 hazy) to `weather_state` (`src/game/weather.cpp`):
  plain arithmetic on the existing noise. It builds up in settled, calm, dry
  spells and clears after rain and in wind, as air behind a front is crisp
  in reality.
- It sets the day's clear visibility between two lighting.json numbers, about
  40 km crisp and 8 km hazy (check typical values before setting them).
  It is visual only: never a shot input.
- Consider a `hazy` preset in the god menu (`weather_kind`).

## 3. Retune and check

- Retune the golden-hour keys and the weather's sun glow in the tour (F2 panel).
- The ball, cup and flag contrast tests may fail looking into the sun: fix the
  palette, not the check.
- Stay inside the 1.0 ms lighting budget (`docs/performance.md`).

Danish courses are flat, so the layering comes from tree lines and the land
panorama rather than ridges; expect it flatter than mountain references.
