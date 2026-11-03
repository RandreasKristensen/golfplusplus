# shot_lab

Hits the bag through the game's own `simulate_shot` and prints what each shot
did: carry and total along the aim, apex, landing angle, how far it rests off
the aim line (positive left) and its time in the air, in metres, degrees and
seconds. In still, dry air on a flat fairway by default, or from a course's
tee towards its pin. For fitting club specs to the reference data in
`tests/fixtures/reference/` (the TrackMan tour averages, the bag's targets)
without running the game. It is the same simulation the game and the server
run: there is no second physics.

```powershell
cmake --build build/test --target golfpp-shot-lab
build\test\golfpp-shot-lab.exe                         # every club, at 1, 0.75 and 0.5
build\test\golfpp-shot-lab.exe --club driver --power 1  # one club, one swing
build\test\golfpp-shot-lab.exe --course marienlyst_golfklub --hole 3 --csv
build\test\golfpp-shot-lab.exe --fit tests\fixtures\reference\bag_targets.json
```

`--fit` bisects each club's `speed_at_99` until a full swing carries its row
of the bag table and prints the shipped value beside the fitted one: copy
the fitted value into the club's item file (`assets/play/items/club/`) when
a spec or the flight changes.
