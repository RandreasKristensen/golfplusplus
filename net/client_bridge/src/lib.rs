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

text_reducer!(stdb_claim_name, "claim_name", |conn, text, done| conn.reducers.claim_name_then(text, done));
text_reducer!(stdb_join_course, "join_course", |conn, text, done| conn.reducers.join_course_then(text, done));
text_reducer!(stdb_emote, "emote", |conn, text, done| conn.reducers.emote_then(text, done));
text_reducer!(stdb_claim_collectible, "claim_collectible", |conn, text, done| conn
    .reducers
    .claim_collectible_reducer_then(text, done));
text_reducer!(stdb_redeem_link_code, "redeem_link_code", |conn, text, done| conn.reducers.redeem_link_code_then(text, done));
reducer!(stdb_leave_room, "leave_room", |conn, done| conn.reducers.leave_room_then(done));
reducer!(stdb_create_group, "create_group", |conn, done| conn.reducers.create_group_then(done));
reducer!(stdb_leave_group, "leave_group", |conn, done| conn.reducers.leave_group_then(done));
reducer!(stdb_return_to_hub, "return_to_hub", |conn, done| conn.reducers.return_to_hub_then(done));
reducer!(stdb_retee, "retee", |conn, done| conn.reducers.retee_then(done));
reducer!(stdb_pick_up_ball, "pick_up_ball", |conn, done| conn.reducers.pick_up_ball_then(done));
reducer!(stdb_create_link_code, "create_link_code", |conn, done| conn.reducers.create_link_code_then(done));

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_join_group(client: *mut StdbClient, group_id: u64) {
    if let Some(c) = self::client(client) {
        c.call("join_group", |conn, done| conn.reducers.join_group_then(group_id, done));
    }
}

/// # Safety
/// See `client`.
#[no_mangle]
pub unsafe extern "C" fn stdb_enter_hole(client: *mut StdbClient, hole_index: i32) {
    if let Some(c) = self::client(client) {
        c.call("enter_hole", |conn, done| conn.reducers.enter_hole_then(hole_index, done));
    }
}

/// # Safety
/// `motion` must point to a valid StdbMotion.
#[no_mangle]
pub unsafe extern "C" fn stdb_update_motion(client: *mut StdbClient, motion: *const StdbMotion) {
    let (Some(c), Some(m)) = (self::client(client), motion.as_ref()) else { return };
    let m = *m;
    c.call("update_motion", |conn, done| {
        conn.reducers.update_motion_then(m.zone, m.mode, m.x, m.y, m.z, m.yaw, m.speed, m.turn_rate, m.client_time, done)
    });
}

/// # Safety
/// `shot` must point to a valid StdbShot whose club id is readable.
#[no_mangle]
pub unsafe extern "C" fn stdb_take_shot(client: *mut StdbClient, shot: *const StdbShot) {
    let (Some(c), Some(s)) = (self::client(client), shot.as_ref()) else { return };
    let s = *s;
    let club = s.club_id.to_string();
    c.call("take_shot", |conn, done| {
        conn.reducers.take_shot_then(s.stroke, s.aim_angle, club, s.power, s.cigarette_active, s.wind_time, done)
    });
}
