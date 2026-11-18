# Instant tee book and accurate message severity

Plan only. No implementation or runtime measurements yet. Remove this plan when
the fixes and their evidence are complete.

## Findings

- The grid is already calculated locally by `tee_times`, but
  [tee_book.cpp](../../src/core/tee_book.cpp) only produces time rows when
  `book_ready` matches the selected course. Selecting a course sends
  `open_tee_book`, clears readiness and blocks menu actions until acknowledged.
  R and the automatic 60-second refresh repeat this blanking and blocking.
- [net_client.cpp](../../src/net/net_client.cpp) also clears readiness and exposed
  slot counts on each open. It accepts a matching status row newer than the
  previous acknowledgement, then publishes the subscribed counts. This makes
  even locally known times depend on a server round trip.
- The expensive cold path is concrete: `open_tee_book` in
  [tee_reducers.h](../../server/golfpp_module/src/tee_reducers.h) calls
  `cached_course(course_id)` just to validate the destination. On a cache miss,
  [module_cache.cpp](../../server/golfpp_module/src/module_cache.cpp) calls
  `build_server_course`, which parses every hole and the world and builds the
  course's ground, collision area and placed scene. Browsing times needs none of
  that. `book_tee` also uses the full course for manifest fields (carts and hole
  count). Parsed manifests and `find_server_course` already exist in
  [server_content.h](../../server/golfpp_module/src/server_content.h).
  This is a likely explanation for a very slow first fetch; its share of the
  observed delay, cache lifetime and warm-request latency need live measurement.
- Travel is supplied twice: `make_render_data` in
  [render_frame.cpp](../../src/core/render_frame.cpp) puts `text_hub_travel` in
  `notice_label`, while [app_hub.cpp](../../src/core/app_hub.cpp) puts it in the
  travel screen. [online_overlay.cpp](../../src/renderer/online_overlay.cpp)
  draws every notice with a red border and `style_error`, regardless of meaning.

## Message audit

| Producer / message | Intended treatment |
|---|---|
| `render_frame.cpp`: travel | One neutral status on the travel screen; remove the duplicate HUD notice. |
| `render_frame.cpp`: pending ball preparation (`hub.pending`) | Neutral progress, never an error. |
| [render_online.cpp](../../src/core/render_online.cpp): `scorecard.waiting_for_group` | Neutral group status. |
| [game_state.cpp](../../src/game/game_state.cpp): water +1 stroke / ball hit +2 strokes | Rules consequences; retain the notice and stroke explanation with non-error presentation. |
| `tee_book.cpp`: pending list/book/cancel | Neutral progress; already neutral in the menu, preserve that. |
| `tee_book.cpp`: slot full/elapsed and reducer refusals | Actual rejected actions; currently inherit the menu's neutral default. Give these explicit error severity. |
| [online_menus.cpp](../../src/core/online_menus.cpp): sign-in, browser, name/code checks, code expiry, joining | Normal status is normally neutral already. Audit retry paths: replacing the text must also replace severity, rather than inherit an earlier error flag. |
| `game_notice` producers in [address_play.cpp](../../src/game/address_play.cpp), [course_session.cpp](../../src/game/course_session.cpp), [golf_bag.cpp](../../src/game/golf_bag.cpp), [golf_cart.cpp](../../src/game/golf_cart.cpp), [hub_flow.cpp](../../src/game/hub_flow.cpp), [my_balls.cpp](../../src/game/my_balls.cpp), [online_play.cpp](../../src/game/online_play.cpp) | Keep errors for refused actions: bag must be set down, occupied tee/seat/bag spot, ball still in play/in cup, missing/expired booking, invalid placement and server refusals. An instruction explaining a refusal is still an error. |

The audit covers assignments to `notice_label`, `game_notice`, menu messages and
uses of `style_error` / `message_is_error`. Do not infer severity from wording,
text-key prefixes or whether a message originated on the server.

## Work

- [ ] Measure the baseline in a disposable local database through the real SDK:
  course selection to grid display, command dispatch to status/count publication,
  reducer time and destination cache/build time. Compare a never-opened large
  course, a second open, another course, manual refresh and periodic refresh.
  Record measurements; do not claim the cold build explains warm delays without
  evidence. Add bounded diagnostics to the existing online-check tooling.
- [ ] Replace destination `cached_course` calls in book opening and booking with
  the existing parsed manifest lookup. Keep reach, room, course eligibility,
  horizon, capacity, cart availability and reservation checks. Full destination
  construction belongs at physical entry, where it is needed; do not hide the
  cost by constructing all courses when the book opens. Measure cold travel too
  so moving this work does not create an unexplained travel regression. Preserve
  loud failure for invalid course content.
- [ ] Show the selected course's locally calculated time grid on its first frame,
  using the validated server-clock estimate and hub day rules. Fetch scoped
  occupancy asynchronously. Unknown counts must be visibly unknown, never zero
  or confirmed available. Preserve last complete same-course counts during
  refresh and distinguish refreshing/stale data from confirmed counts.
- [ ] Separate list-fetch state from booking/cancellation pending state. Browsing,
  switching courses, scrolling and backing out must work during a fetch; refresh
  must not clear rows or reset the selection to another time. Preserve selection
  by scheduled time as the horizon advances. Keep mutation requests single-flight
  and confirmed by reservation rows; do not predict a successful booking.
- [ ] Preserve authoritative validation and scoped subscriptions. Reject late
  results for another course or an obsolete request, including A -> B -> A;
  publish status and counts coherently, and clear retained data on session/room
  teardown. A failed list request must end its loading state, show an actual
  error and permit retry. No unbounded wait or retry loop. Booking can rely on
  the server's capacity check while counts refresh; it must preserve the original
  reservation on refusal.
- [ ] Carry explicit message severity from producers through render data to HUD
  and menus, reusing existing styles. Give progress one owning surface: travel
  uses its screen; other statuses use a neutral notice where needed. Check text,
  border and background, not only font colour. Keep real failures visible and
  clear obsolete progress on acceptance, refusal, cancellation and teardown.
  Ensure progress cannot overwrite a current actionable error.
- [ ] Update only affected rules/comments/tests with the resulting contract,
  including the current acknowledgement-gated wording in
  [networking_rules.md](../networking_rules.md). If request correlation requires
  schema/view/reducer changes, bump `protocol_version`, regenerate Rust bindings
  and change both C bridge declarations together. No new dependency is needed.

## Verification and acceptance

- [ ] Native regressions in `hub_flow_tests`, `net_client_tests` and
  `tee_rules_tests`: immediate grid with delayed counts, empty course, live count
  changes, refresh without blanking, time-based selection, day rollover, full
  slots, rapid course changes/late results, failed fetch/retry, pending mutations,
  refused replacement preserving its row, reconnect and offline behavior.
- [ ] `render_frame_tests`, `online_menus_tests` and GL-free overlay/content
  checks prove normal travel, ball preparation, group waiting and penalty events
  never use error styling; real refusals still do. Assert no duplicate travel
  notice and no inherited error colour after retry. Check 4:3, 16:9 and 21:9 for
  clipping, and inspect the actual travel screen and notice box in the game.
- [ ] Run separately, stopping on failure: `cmake --preset test`,
  `cmake --build build/test`, then `./build/test/golf++-tests.exe`.
  Build the changed server/bridge and run the relevant real-SDK booking lifecycle
  in a fresh disposable database per
  [online_check](../../tooling/net/online_check/README.md); fake transport tests
  cannot prove reducer latency or view delivery.
- [ ] Done when course times appear on the next rendered frame regardless of
  fetch latency, refreshing never interrupts browsing, and opening/booking never
  builds destination terrain. Record before/after cold and warm server timings.
  Occupancy and booking remain authoritative, and red error presentation is used
  only for actual failures/refusals across every audited path.
