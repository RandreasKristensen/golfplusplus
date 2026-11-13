//! golf++'s connection to its SpacetimeDB server (server/golfpp_module),
//! behind the small C API declared in `src/net/stdb_bridge.h`. The game
//! drives everything from its main thread with `stdb_frame_tick` and reads
//! what happened with `stdb_poll`; only signing in and opening the WebSocket
//! run on background threads (client.rs), so a frame never waits on the
//! network. This file is the C API.

mod client;
mod events;
mod ffi;
mod login;
pub mod module_bindings;

use client::StdbClient;
use events::Event;
use ffi::*;
use login::AuthConfig;
use module_bindings::*;
use std::os::raw::c_char;

/// # Safety
/// `client` must come from `stdb_create` and not have been destroyed.
unsafe fn client<'a>(client: *mut StdbClient) -> Option<&'a mut StdbClient> {
    client.as_mut()
}

/// Every struct's size and field offset (ffi::layout), so the C++ side can
/// check both sides agree on the layout. Writes up to `max` values to `out`
/// and returns how many there are in all.
///
/// # Safety
/// `out` must have room for `max` values, or be null with `max` 0.
#[no_mangle]
pub unsafe extern "C" fn stdb_layout(out: *mut usize, max: usize) -> usize {
    let layout = ffi::layout();
    if !out.is_null() {
        for (i, value) in layout.iter().take(max).enumerate() {
            *out.add(i) = *value;
        }
    }
    layout.len()
}

/// # Safety
/// `config` must point to a valid StdbConfig whose strings are readable.
#[no_mangle]
pub unsafe extern "C" fn stdb_create(config: *const StdbConfig) -> *mut StdbClient {
    let Some(config) = config.as_ref() else { return std::ptr::null_mut() };
    let auth = AuthConfig {
        issuer: config.auth_issuer.to_string(),
        client_id: config.auth_client_id.to_string(),
        scopes: config.auth_scopes.to_string(),
        authorization_endpoint: config.auth_authorization_endpoint.to_string(),
        token_endpoint: config.auth_token_endpoint.to_string(),
        page_signed_in: config.page_signed_in.to_string(),
        page_failed: config.page_failed.to_string(),
    };
    let client = StdbClient::new(config.server_uri.to_string(), config.database.to_string(), auth, config.anonymous);
    Box::into_raw(Box::new(client))
}

/// # Safety
/// `client` must come from `stdb_create`; it is invalid afterwards.
#[no_mangle]
pub unsafe extern "C" fn stdb_destroy(client: *mut StdbClient) {
    if !client.is_null() {
        drop(Box::from_raw(client));
    }
}

/// Signs in and connects: anonymously when configured so, else silently
/// with a stored sign-in or (unless `silent_only`) through the browser.
/// False when already signing in or connected.
///
/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_begin_login(client: *mut StdbClient, silent_only: bool) -> bool {
    self::client(client).is_some_and(|c| c.begin_login(silent_only))
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_cancel_login(client: *mut StdbClient) {
    if let Some(c) = self::client(client) {
        c.cancel_login();
    }
}

/// Disconnects, keeping the stored sign-in.
///
/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_disconnect(client: *mut StdbClient) {
    if let Some(c) = self::client(client) {
        c.disconnect();
    }
}

/// Disconnects and forgets the stored sign-in.
///
/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_sign_out(client: *mut StdbClient) {
    if let Some(c) = self::client(client) {
        c.sign_out();
    }
}

/// Advances sign-in and the connection, running row and reducer callbacks.
/// Never blocks. Returns 1 while connected, else 0.
///
/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_frame_tick(client: *mut StdbClient) -> i32 {
    let Some(c) = self::client(client) else { return 0 };
    c.tick();
    i32::from(c.connection.is_some())
}

/// Copies up to `max` events into `out` and returns how many. Strings in
/// them stay valid until the next call.
///
/// # Safety
/// `out` must have room for `max` events.
#[no_mangle]
pub unsafe extern "C" fn stdb_poll(client: *mut StdbClient, out: *mut StdbEvent, max: usize) -> usize {
    let Some(c) = self::client(client) else { return 0 };
    if out.is_null() {
        return 0;
    }
    c.arena.clear();
    let taken: Vec<Event> = match c.queue.lock() {
        Ok(mut queue) => {
            let count = queue.len().min(max);
            queue.drain(..count).collect()
        }
        Err(_) => return 0,
    };
    for (i, event) in taken.iter().enumerate() {
        *out.add(i) = events::to_c(&mut c.arena, event);
    }
    taken.len()
}

/// Subscribes to SQL queries; returns a handle for stdb_unsubscribe (0 when
/// not connected). SubscriptionApplied or SubscriptionFailed follows.
///
/// # Safety
/// `queries` must point to `count` readable strings.
#[no_mangle]
pub unsafe extern "C" fn stdb_subscribe(client: *mut StdbClient, queries: *const StdbString, count: usize) -> u32 {
    let Some(c) = self::client(client) else { return 0 };
    if queries.is_null() || count == 0 {
        return 0;
    }
    let sql: Vec<String> = (0..count).map(|i| (*queries.add(i)).to_string()).collect();
    c.subscribe(sql)
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_unsubscribe(client: *mut StdbClient, subscription: u32) {
    if let Some(c) = self::client(client) {
        c.unsubscribe(subscription);
    }
}

// --- Reducers ------------------------------------------------------------------

macro_rules! reducer {
    ($c_name:ident, $reducer:literal, |$conn:ident, $done:ident| $call:expr) => {
        /// # Safety
        /// See `client`.
        #[no_mangle]
        pub unsafe extern "C" fn $c_name(client: *mut StdbClient) {
            if let Some(c) = self::client(client) {
                c.call($reducer, |$conn, $done| $call);
            }
        }
    };
}

macro_rules! text_reducer {
    ($c_name:ident, $reducer:literal, |$conn:ident, $text:ident, $done:ident| $call:expr) => {
        /// # Safety
        /// `text` must point to `len` readable bytes.
        #[no_mangle]
        pub unsafe extern "C" fn $c_name(client: *mut StdbClient, text: *const c_char, len: usize) {
            let $text = StdbString { data: text, len }.to_string();
            if let Some(c) = self::client(client) {
                c.call($reducer, |$conn, $done| $call);
            }
        }
    };
}

#[no_mangle]
pub unsafe extern "C" fn stdb_dap_up(client: *mut StdbClient, expected_room: u64, target: u64) {
    if let Some(c) = self::client(client) { c.call("dap", |conn, done| conn.reducers.dap_then(expected_room, target, done)); }
}

text_reducer!(stdb_claim_name, "claim_name", |conn, text, done| conn.reducers.claim_name_then(text, done));
#[no_mangle]
pub unsafe extern "C" fn stdb_use_item(client: *mut StdbClient, expected_room: u64, text: *const c_char, len: usize) {
    let text = StdbString { data: text, len }.to_string();
    if let Some(c) = self::client(client) {
        c.call("use_item", |conn, done| conn.reducers.use_item_then(expected_room, text, done));
    }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_take_club(client: *mut StdbClient, expected_room: u64, text: *const c_char, len: usize) {
    let text = StdbString { data: text, len }.to_string();
    if let Some(c) = self::client(client) {
        c.call("take_club", |conn, done| conn.reducers.take_club_then(expected_room, text, done));
    }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_claim_collectible(client: *mut StdbClient, expected_room: u64, text: *const c_char, len: usize) {
    let text = StdbString { data: text, len }.to_string();
    if let Some(c) = self::client(client) {
        c.call("claim_collectible", |conn, done| conn.reducers.claim_collectible_reducer_then(expected_room, text, done));
    }
}
text_reducer!(stdb_redeem_link_code, "redeem_link_code", |conn, text, done| conn.reducers.redeem_link_code_then(text, done));
#[no_mangle]
pub unsafe extern "C" fn stdb_leave_room(client: *mut StdbClient, expected_room: u64) {
    if let Some(c) = self::client(client) { c.call("leave_room", |conn, done| conn.reducers.leave_room_then(expected_room, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_give_up_hole(client: *mut StdbClient, expected_room: u64) {
    if let Some(c) = self::client(client) { c.call("give_up_hole", |conn, done| conn.reducers.give_up_hole_then(expected_room, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_place_practice_ball(client: *mut StdbClient, expected_room: u64, x: f32, y: f32, z: f32) {
    if let Some(c) = self::client(client) { c.call("place_practice_ball", |conn, done|
        conn.reducers.place_practice_ball_then(expected_room, x, y, z, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_pick_up_ball(client: *mut StdbClient, expected_room: u64) {
    if let Some(c) = self::client(client) { c.call("pick_up_ball", |conn, done| conn.reducers.pick_up_ball_then(expected_room, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_leave_cart(client: *mut StdbClient, expected_room: u64) {
    if let Some(c) = self::client(client) { c.call("leave_cart", |conn, done| conn.reducers.leave_cart_then(expected_room, done)); }
}
reducer!(stdb_create_link_code, "create_link_code", |conn, done| conn.reducers.create_link_code_then(done));

/// # Safety
/// `course_id` must point to `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn stdb_join_course(client: *mut StdbClient, course_id: *const c_char, len: usize, takes_cart: bool) {
    let course = StdbString { data: course_id, len }.to_string();
    if let Some(c) = self::client(client) {
        c.call("join_course", |conn, done| conn.reducers.join_course_then(course, takes_cart, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_board_cart(client: *mut StdbClient, expected_room: u64, owner: u64) {
    if let Some(c) = self::client(client) {
        c.call("board_cart", |conn, done| conn.reducers.board_cart_then(expected_room, owner, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_right_cart(client: *mut StdbClient, expected_room: u64, owner: u64) {
    if let Some(c) = self::client(client) {
        c.call("right_cart", |conn, done| conn.reducers.right_cart_then(expected_room, owner, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_tee_up(client: *mut StdbClient, expected_room: u64, hole_index: i32, give_up: bool) {
    if let Some(c) = self::client(client) {
        c.call("tee_up", |conn, done| conn.reducers.tee_up_then(expected_room, hole_index, give_up, done));
    }
}

/// # Safety
/// `motion` must point to a valid StdbMotion and `steps` to a valid
/// StdbSteps whose `runs` holds `run_count` runs.
#[no_mangle]
pub unsafe extern "C" fn stdb_update_motion(client: *mut StdbClient, expected_room: u64, motion: *const StdbMotion, steps: *const StdbSteps) -> bool {
    let (Some(c), Some(m), Some(s)) = (self::client(client), motion.as_ref(), steps.as_ref()) else { return false };
    let m = *m;
    let s = *s;
    let runs: Vec<MoveRunRow> = if s.runs.is_null() || s.run_count == 0 {
        Vec::new()
    } else {
        std::slice::from_raw_parts(s.runs, s.run_count)
            .iter()
            .map(|r| MoveRunRow {
                kind: r.kind,
                steps: r.steps,
                forward: r.forward,
                side: r.side,
                run: r.run,
                jump: r.jump,
                yaw: r.yaw,
                steer: r.steer,
                paced: r.paced,
                crouched: r.crouched,
            })
            .collect()
    };
    c.call("update_motion", |conn, done| {
        conn.reducers.update_motion_then(expected_room, m.mode, m.x, m.y, m.z, m.yaw, m.speed, m.turn_rate, m.client_time, s.first_step,
                                         s.start_x, s.start_y, s.start_z, runs, m.setup.aim, m.setup.face_open,
                                         m.setup.ball_position, m.setup.backswing, m.crouched, done)
    })
}

/// # Safety
/// `motion` must point to a valid StdbMotion.
#[no_mangle]
pub unsafe extern "C" fn stdb_update_ride(client: *mut StdbClient, expected_room: u64, motion: *const StdbMotion) {
    let (Some(c), Some(m)) = (self::client(client), motion.as_ref()) else { return };
    let m = *m;
    c.call("update_ride", |conn, done| {
        conn.reducers.update_ride_then(expected_room, m.x, m.y, m.z, m.yaw, m.speed, m.turn_rate, m.client_time, done)
    });
}

/// # Safety
/// `shot` must point to a valid StdbShot whose club id is readable.
#[no_mangle]
pub unsafe extern "C" fn stdb_take_shot(client: *mut StdbClient, expected_room: u64, shot: *const StdbShot) {
    let (Some(c), Some(s)) = (self::client(client), shot.as_ref()) else { return };
    let s = *s;
    let club = s.club_id.to_string();
    let flags_out = if s.flags_out.is_null() || s.flags_out_count == 0 {
        Vec::new()
    } else {
        std::slice::from_raw_parts(s.flags_out, s.flags_out_count).to_vec()
    };
    c.call("take_shot", |conn, done| {
        conn.reducers.take_shot_then(expected_room, s.stroke, s.aim_angle, s.face_open, s.ball_position, s.backswing, s.strike_timing,
                                     club, s.shot_at, flags_out, done)
    });
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_set_flag(client: *mut StdbClient, expected_room: u64, hole: i32, out: bool) {
    if let Some(c) = self::client(client) {
        c.call("set_flag", |conn, done| conn.reducers.set_flag_then(expected_room, hole, out, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_place_bag(client: *mut StdbClient, expected_room: u64, place: u8, seat: u8, cart_owner: u64, x: f32, y: f32, z: f32,
                                        yaw: f32) {
    if let Some(c) = self::client(client) {
        c.call("place_bag", |conn, done| conn.reducers.place_bag_then(expected_room, place, seat, cart_owner, x, y, z, yaw, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_set_golfer(client: *mut StdbClient, hand: u8, natural_shape: u8, favourite_colour: u8) {
    if let Some(c) = self::client(client) {
        c.call("set_golfer", |conn, done| conn.reducers.set_golfer_then(hand, natural_shape, favourite_colour, done));
    }
}

/// # Safety
/// `client` must be the live client returned by stdb_create.
#[no_mangle]
pub unsafe extern "C" fn stdb_take_wheel(client: *mut StdbClient, expected_room: u64) {
    let Some(c) = self::client(client) else { return };
    c.call("take_wheel", |conn, done| conn.reducers.take_wheel_then(expected_room, done));
}

/// # Safety
/// `client` must be the live client returned by stdb_create.
#[no_mangle]
pub unsafe extern "C" fn stdb_leave_wheel(client: *mut StdbClient, expected_room: u64) {
    let Some(c) = self::client(client) else { return };
    c.call("leave_wheel", |conn, done| conn.reducers.leave_wheel_then(expected_room, done));
}

#[no_mangle]
pub unsafe extern "C" fn stdb_open_tee_book(client: *mut StdbClient, room: u64, course: *const c_char, len: usize) {
    let course = StdbString { data: course, len }.to_string();
    if let Some(c) = self::client(client) { c.call("open_tee_book", |conn, done| conn.reducers.open_tee_book_then(room, course, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_book_tee(client: *mut StdbClient, room: u64, booking: u64, course: *const c_char, len: usize, at: i64) {
    let course = StdbString { data: course, len }.to_string();
    if let Some(c) = self::client(client) { c.call("book_tee", |conn, done| conn.reducers.book_tee_then(room, booking, course, at, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_cancel_tee(client: *mut StdbClient, room: u64, booking: u64) {
    if let Some(c) = self::client(client) { c.call("cancel_tee", |conn, done| conn.reducers.cancel_tee_then(room, booking, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_take_bus(client: *mut StdbClient, room: u64, booking: u64, cart: bool) {
    if let Some(c) = self::client(client) { c.call("take_bus", |conn, done| conn.reducers.take_bus_then(room, booking, cart, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_return_to_hub(client: *mut StdbClient, room: u64) {
    if let Some(c) = self::client(client) { c.call("return_to_hub", |conn, done| conn.reducers.return_to_hub_then(room, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_start_booked_round(client: *mut StdbClient, room: u64, booking: u64) {
    if let Some(c) = self::client(client) { c.call("start_booked_round", |conn, done| conn.reducers.start_booked_round_then(room, booking, done)); }
}
#[no_mangle]
pub unsafe extern "C" fn stdb_leave_group(client: *mut StdbClient, room: u64, booking: u64) {
    if let Some(c) = self::client(client) { c.call("leave_group", |conn, done| conn.reducers.leave_group_then(room, booking, done)); }
}
