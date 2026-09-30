# Steam todo

What has to be true before golf++ gets a Steam "Coming Soon" page (wishlists),
and later before release. Researched 2026-09-30; re-check Steamworks docs before
acting, policies change.

**Keep this in sync:** when an item is done, remove it (or tick it, if it's worth
keeping as a record for the store review). If it changed the code, the content rules
or the build (e.g. renamed courses, a credits screen, a `licenses/` folder, Steam
login), update `AGENTS.md` in the same change so it describes the result, and drop
any "planned" or "due to be" wording there about it. Decided items don't stay here as
open questions: move the decision into `AGENTS.md` or `docs/multiplayer_plan.md` and
delete the item.

---

## 1. Legal and licensing (blocks the store page)

### Rename trademarked courses
- [ ] `augusta_national_golf_club.json` and `old_course.json` use protected names
  (Augusta National, St Andrews / Old Course). Rename them to fictional names.
- [ ] Never show those names, or clearly recognisable footage of them, on the
  store page, in the trailer or in social content.

### Ask real clubs for permission
- [ ] For any real course we keep under its real name (e.g. Marienlyst), get
  written permission from the club. Small clubs often say yes to free promotion.

### OpenStreetMap attribution (ODbL)
- [ ] Show "© OpenStreetMap contributors" in the in-game credits (and ideally
  the course select screen). Nothing in the game credits OSM today.
- [ ] Check the ODbL share-alike terms for our changed course data and decide
  how to publish it (e.g. a public repo with the course JSON).

### Audio and asset licences
- [ ] Record the source and licence of every file in `assets/audio/` and
  `assets/icons/`. Replace anything that doesn't allow commercial use.

### Library licences
- [ ] Ship licence texts for SDL2, SDL2_mixer, GLM, nlohmann/json (and
  SpacetimeDB SDK / Steamworks SDK if added) in a `licenses/` folder next to the game.

### Name check
- [ ] Search Steam and trademark databases for "golf++". Check the "++" works in
  Steam search, hashtags and video titles.

---

## 2. Steamworks account

### Partner onboarding
- [ ] Create the Steamworks partner account, complete identity, tax and bank
  verification. This can take days to weeks, so start early.

### Steam Direct fee
- [ ] Pay the $100 app fee (recouped after $1,000 in sales). This creates the app
  id used for the store page and builds.

---

## 3. Store page ("Coming Soon")

### Capsule art
- [ ] Make all required capsule images (header, small, main, vertical, library
  hero/logo) in Steam's exact sizes. They should read clearly at thumbnail size.

### Screenshots
- [ ] At least 5 real in-game screenshots, with the CRT pass on (it always is).
  No concept art or mockups; Valve rejects those.

### Trailer
- [ ] A 30–60 s trailer that shows golf in the first 5 seconds: swing, ball
  flight, the VHS look, the hub, cart drifting, skill level-ups.

### Description and tags
- [ ] Short description (one hook sentence) and long description. List
  multiplayer as "planned" until it ships.
- [ ] Pick tags that match similar successful games (golf, retro, relaxing, RPG-lite).

### Content survey and AI disclosure
- [ ] Fill in the content survey. AI-written code does not need disclosure (Jan 2026
  rules), but any AI-generated art, audio, text or trailer content does.
- [ ] Audit assets for AI-generated content. Prefer hand-made assets for anything
  players see or hear.

### Review timing
- [ ] Submit for review at least 7 business days before the page should go live,
  then press "Post as Coming Soon" after approval.

---

## 4. Game readiness for footage

### One polished course
- [ ] One course that looks great from every camera angle in the trailer:
  clean splines, trees, hole starts, cart roads.

### Core loop feel
- [ ] Swing, ball flight, putting and the hub walk/cart loop feel good enough to
  show in 10-second clips without explanation.

### Credits screen
- [ ] A credits screen in the menu (needed for OSM attribution and licences anyway).

---

## 5. Multiplayer on Steam (see `multiplayer_plan.md`)

Steam sign-in (including linking beta accounts) is planned as Phase 6 of
`multiplayer_plan.md`, blocked on the app id. Offline play is a rule in `AGENTS.md`.

### Privacy and GDPR (EU developer)
- [ ] Write a privacy policy (what we store, where, how long) and link it in Steamworks.
- [ ] Let players delete their account and data.

### Names and moderation
- [ ] Filter player names, let players report names, and add tools to rename or ban.
  Chat, if added later, needs the same.

### Cheating
- [ ] The server decides shots, scores and XP. Swing power is still trusted from
  the client. That's fine for casual play; fix it before leaderboards or ranked play.

### Server costs and uptime
- [ ] Estimate the Maincloud cost at launch player counts and set up monitoring.
  The server becomes a permanent obligation once the game is sold.

---

## 6. Build and release (later)

### Steam build pipeline
- [ ] Upload builds with SteamPipe. A Windows release build from `tooling/gb.ps1`
  should run on a clean machine (all DLLs, the `assets/` folder, no dev paths).

### Build review
- [ ] Valve reviews the build before release. It must start, run and match the
  store page. The page must be Coming Soon for at least 2 weeks before release.

### Steam Deck / controller
- [ ] Optional but valuable: controller support and a Deck-friendly UI scale.

---

## 7. Marketing

### Content before launch
- [ ] Start posting short clips as soon as the page is live. Every post links to
  the Steam page. Wishlists are the metric.

### Steam Next Fest
- [ ] You can only enter one Next Fest before release. Save it for a polished demo.

### Demo
- [ ] A demo with one course and the hub loop, released during or just before Next Fest.
