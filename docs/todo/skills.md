# Skills: the level refactor

The plan for real skills and levels (the owner's list is in `docs/ideas.md`:
club skills, XP by how a shot went, smoking and drinking, fitness, driving,
exploration, social, caddying). The tables and rules they stand on are
built (`docs/networking_rules.md` "Progress lasts").

## 1. What a skill takes now

- **A new skill** is one entry in `assets/play/progression/skills.json`: an
  id, an icon and a `curve`. No table, migration or code: a skill with no
  XP row reads 0 XP, level 1.
- **A curve** is integer XP thresholds in `curves.json`
  (`src/game/skill_catalog.h`). A curve never asks more XP for a level than
  the last shipped copy (`tests/fixtures/progression/shipped_curves.json`)
  unless the owner decides it.
- **Paying XP** goes through `progress_rules` (offline and on the server
  alike) and, online, `store_progress`: every award is an `xp_award` row with
  a key naming its event, so one event pays once. XP is 64-bit and never
  capped.
- **Retiring a skill** keeps it in `skills.json` with `retired: true`: its
  progress stays and shows, nothing pays into it any more.
- **Levels in shots**: a golfer's levels are every listed skill's
  (`skill_levels`), offline from the save and online from the server's rows
  (`golfer_of`); the shot event carries the levels it was played with, so
  replays agree after a level-up.

## 2. Gap

Shot ability reads skills no one has yet: `club.<class>` and
`shape.<shape>` (`ability_for` in `src/game/shot_ability.cpp`) are not in
`skills.json`, so they read the top level (`golfer_top_level`).

## 3. The refactor

1. Settle the skill list and ids (they are forever once shipped), and which
   of them shot ability reads: `ability_for`'s ids and `skills.json` become
   one list, checked at load like the reward skills.
2. Add them to `skills.json`. `golfer_of` and `my_golfer` then fill their
   levels from XP, and the shot event already carries them.
3. XP by outcome (`docs/ideas.md`): new reward rules as data in
   `rewards.json`, paid through `progress_rules`, so offline and the server
   give the same.
4. Every skill's XP display and drop through the existing skills panel.

Tests: a retired skill loading, a curve change against old XP, a level-up
shot replayed by a client, the award sum against `golfer_skill`.

## 4. Decision for the owner

Existing `golf_swing` XP: nothing records which club earned it. In the alpha
it can be dropped, or spread over the club skills by a rule.
