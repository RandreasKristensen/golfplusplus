# Ideas

Product direction, architecture TODOs, and backlog notes for golf++.

## Long-Term Direction

- golf++ is headed toward a lo-fi open-course golf RPG, not just isolated golf rounds.
- Courses remain selected from menus for now, but each course should become a roamable hub with NPCs, signs, pickups, shops, and hole starts.
- Quests stay, but become NPC/world-driven instead of only text-menu progression.
- RuneScape-style skills should track repeated activities like golfing, putting, smoking, drinking, drifting, collecting, gambling, and course exploration.
- Unlocks should come from skills, money, quests, discoveries, NPC relationships, course completion, and shops.
- Multiplayer is a long-term goal, but should start with low-risk forms: local, turn-based, ghost, and async before real-time online.
- Backend work should stay light until it has a clear job: cloud saves, score submissions, ghost data, accounts, or cosmetics. Do not build an OSRS/Path of Exile-style authoritative backend before the RPG and async multiplayer loops prove they need it.

## Near-Term Architecture TODOs

- Add generic `skill_id -> xp/level` progression model.
- Add data-driven XP awards for actions like swing, hole complete, drift, smoke, drink, pickup, and quest complete.
- Expand save data with versioned skill progress, inventory, active quests, quest flags, and discovered course/world state.
- Replace bespoke activity state growth with generic systems where possible; avoid adding one-off fields for every new action.
- Add item/inventory definitions for balls, clubs, cosmetics, consumables, and collectibles.
- Add unlock rule checks instead of direct `unlocked_items` checks everywhere.
- Add NPC definitions placed in course/hole JSON or separate course-world JSON.
- Add interaction system for nearby NPCs, signs, shops, pickups, and hole starts.
- Upgrade quests from simple dialogue rewards into objective chains with conditions and world flags.
- Split large files before major feature additions: renderer UI drawing, app flow/menu handling, and game update sub-systems.
- Define a multiplayer/session boundary before adding network code. Local play, ghost replay data, and async challenge submissions should not leak directly into core single-player `game_state`.

## Feature Backlog

### Skills

- Golfing
- Putting
- Driving accuracy
- Smoking
- Drinking
- Drifting
- Ball finding (you can run out of balls, and balls have stats? you can buy balls if you run out find others on courses.)
- Gambling

### NPCs

- Random course NPCs
- Recurring weirdos
- Shopkeepers
- Challenge golfers
- Quest givers

### World

- Course hubs
- Signs
- Pickups
- Shops
- Lost balls
- Weather
- Discoverable shortcuts

### Gear

- Named parody clubs
- Collectible balls
- Custom club names
- Shop inventory
- Rare unlocks

### Quests

- NPC dialogue
- Skill requirements
- Course challenges
- Repeatable tasks
- Branching outcomes

### Multiplayer

- Hot-seat first
- Ghost balls second
- Async score/challenge sharing third
- Lightweight backend for saves, ghost data, score submissions, accounts, or cosmetics only when needed
- Real-time later

## Scratchpad Migrated From Notes

### Bugs

- The drawn-on club is facing the wrong way.
- Tweak the camera. It does not feel correct. Add settings like FOV, height off ground, and distance from ball when standing beside it and swinging.

### Map Editor

- Water cannot be resized in the editor.
- Make an auto map generator from top-down pictures of courses. Can this be done?
- Maybe asking Codex or Claude to generate some courses would work.
- Mangler.

### Textures

- Weather would be huge, at least rain where shots go a bit shorter.
- Add textures on the ground to make movement have more context; it is trippy right now.
- Higher pixel rendering density.
- Fullscreen option scaling to any resolution natively.

### UI

- Wind direction UI in the game. Maybe in the range finder.
- The range should also be smaller in the range finder.
- Compass.

### Functionality

- RuneScape-style skill progression for unlocks.
- Allow level 99 smoking, drinking, drifting, and similar strange skills.
- More RPG elements instead of only quests, or both: quests from random NPCs around courses.
- Keep menu navigation between courses, but trend toward a basically open-world course feel.
- Add a scorecard where the player can write down a score manually, then compare it with the actual count at the end.
- Explore golf carts, possibly as an instant left-shift spawn or UI/projection-style overlay if full 3D is not worth it yet.
- Pick up balls around the course that are not yours if you do not find your ball; you have to drop it, with a timer from when you shoot.
- Let players name clubs and collect clubs.
- Buy clubs in a shop with made-up names like Haitormade Wedges, Callitaday driver, putter, iron, and similar gear.

### Devops

- Improve the testing framework for Codex to run tests better.
- Generate holes through the generator and maybe play the game to check it actually builds.
- Investigate whether SDL interaction automation or an MCP-style tool can help.
