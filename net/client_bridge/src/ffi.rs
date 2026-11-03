//! The plain C types of the bridge's API. `src/net/stdb_bridge.h` declares
//! the same types for C++; keep both in the same order and layout.

use std::os::raw::c_char;

/// UTF-8 text, not NUL-terminated. Strings the bridge returns stay valid
/// until the next `stdb_poll`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbString {
    pub data: *const c_char,
    pub len: usize,
}

impl StdbString {
    pub const EMPTY: StdbString = StdbString { data: std::ptr::null(), len: 0 };

    /// The text, or empty when it is null or not UTF-8.
    ///
    /// # Safety
    /// `data` must point to `len` readable bytes, or be null.
    pub unsafe fn to_string(self) -> String {
        if self.data.is_null() || self.len == 0 {
            return String::new();
        }
        let bytes = std::slice::from_raw_parts(self.data as *const u8, self.len);
        String::from_utf8(bytes.to_vec()).unwrap_or_default()
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbConfig {
    pub server_uri: StdbString,
    pub database: StdbString,
    pub auth_issuer: StdbString,
    pub auth_client_id: StdbString,
    pub auth_scopes: StdbString,
    pub auth_authorization_endpoint: StdbString,
    pub auth_token_endpoint: StdbString,
    /// What the browser shows after signing in, and after a sign-in that
    /// did not finish (the game's string table).
    pub page_signed_in: StdbString,
    pub page_failed: StdbString,
    /// Connect without signing in (servers with allow_anonymous only).
    pub anonymous: bool,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StdbLoginMethod {
    Browser = 0,
    Anonymous = 1,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StdbEventKind {
    LoginWaitingForBrowser = 0,
    LoginFailed = 1,
    SignedIn = 2,
    Connected = 3,
    Disconnected = 4,
    SubscriptionApplied = 5,
    SubscriptionFailed = 6,
    Row = 7,
    ReducerFailed = 8,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StdbTable {
    Player = 0,
    MySkills = 1,
    MyGolferProgress = 2,
    MyCompletedCourses = 3,
    MyCollected = 4,
    MyWorldFlags = 5,
    Room = 6,
    RoomMember = 7,
    GolfGroup = 8,
    AvatarMotion = 9,
    Ball = 10,
    ShotEvent = 11,
    ItemUseEvent = 12,
    MyAccount = 13,
    MyLinkCode = 14,
    MyLinkStatus = 15,
    ServerProtocol = 16,
    GolfCart = 17,
    PlayerLoadout = 18,
    RunningEffect = 19,
    PlayerBag = 20,
    GreenFlag = 21,
    RideMotion = 22,
    ServerContentHash = 23,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StdbRowChange {
    Insert = 0,
    /// The row's new value; the client keys rows by their primary key.
    Update = 1,
    Delete = 2,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbPlayer {
    pub account_id: u64,
    pub display_name: StdbString,
    pub online: bool,
    /// handedness in game/shot_setup.h.
    pub hand: u8,
    /// natural_shape in game/golfer.h, or natural_shape_unset.
    pub natural_shape: u8,
    /// game/gear_paint.h, 0 for none.
    pub favourite_colour: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbGolferSkill {
    pub id: u64,
    pub golfer_id: u64,
    pub skill_id: StdbString,
    pub xp: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbGolferProgress {
    pub golfer_id: u64,
    pub account_id: u64,
    pub scope_id: u32,
    pub holes_completed: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbCompletedCourse {
    pub id: u64,
    pub golfer_id: u64,
    pub course_id: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbCollected {
    pub id: u64,
    pub golfer_id: u64,
    pub collectible_id: StdbString,
    pub repeatable: bool,
    pub claim_count: i32,
    /// -1: never.
    pub claimed_at_holes_completed: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbWorldFlag {
    pub id: u64,
    pub golfer_id: u64,
    pub flag: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbRoom {
    pub room_id: u64,
    pub course_id: StdbString,
    pub player_count: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbRoomMember {
    pub account_id: u64,
    pub room_id: u64,
    pub group_id: u64,
    pub round_strokes: *const i32,
    pub round_strokes_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbGolfGroup {
    pub group_id: u64,
    pub room_id: u64,
    pub leader: u64,
    pub member_count: i32,
}

/// A setup at address as sent (game/sent_setup.h).
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct StdbSetup {
    pub aim: u16,
    pub face_open: i16,
    pub ball_position: i8,
    pub backswing: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbMotion {
    pub mode: u8,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    pub speed: f32,
    pub turn_rate: f32,
    pub client_time: f64,
    pub setup: StdbSetup,
    pub crouched: bool,
}

/// A run of fixed steps with the same input (game/movement_step.h move_run).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbMoveRun {
    pub kind: u8,
    pub steps: u16,
    pub forward: i8,
    pub side: i8,
    pub run: bool,
    pub jump: bool,
    pub yaw: u16,
    pub steer: i16,
}

/// The fixed steps a motion carries: numbered on from first_step, the first
/// on foot taken from start_*.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbSteps {
    pub first_step: u64,
    pub start_x: f32,
    pub start_y: f32,
    pub start_z: f32,
    pub runs: *const StdbMoveRun,
    pub run_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbAvatarMotion {
    pub account_id: u64,
    pub room_id: u64,
    pub motion: StdbMotion,
    pub server_time_micros: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbRideMotion {
    pub rider: u64,
    pub owner: u64,
    pub motion: StdbMotion,
    pub server_time_micros: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbBall {
    pub account_id: u64,
    pub room_id: u64,
    pub hole: i32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub stroke_count: i32,
    /// UTC milliseconds.
    pub last_shot_at: i64,
    pub in_cup: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbGolfCart {
    pub owner: u64,
    pub room_id: u64,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    /// 0: nobody.
    pub passenger: u64,
    /// cart_condition (physics/cart_dynamics.h).
    pub condition: u8,
    pub driver: u64,
    pub motion_id: f64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbShot {
    pub stroke: i32,
    pub aim_angle: f32,
    pub face_open: f32,
    pub ball_position: f32,
    pub backswing: f32,
    pub strike_timing: f32,
    pub club_id: StdbString,
    /// UTC milliseconds, on the server's clock as the client estimates it.
    pub shot_at: i64,
    /// The holes whose flag the client has out.
    pub flags_out: *const i32,
    pub flags_out_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbSnapshotEntry {
    /// snapshot_kind in game/shot_snapshot.h.
    pub kind: u8,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    pub shooter: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbSkillLevel {
    pub skill_id: StdbString,
    pub level: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbShotEvent {
    pub room_id: u64,
    pub account_id: u64,
    pub hole: i32,
    pub shot: StdbShot,
    pub hand: u8,
    pub natural_shape: u8,
    pub spread_scale: f32,
    pub levels: *const StdbSkillLevel,
    pub levels_count: usize,
    pub seed: u64,
    pub snapshot: *const StdbSnapshotEntry,
    pub snapshot_count: usize,
    pub start_x: f32,
    pub start_y: f32,
    pub start_z: f32,
    pub rest_x: f32,
    pub rest_y: f32,
    pub rest_z: f32,
    pub holed: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbItemUseEvent {
    pub room_id: u64,
    pub account_id: u64,
    pub item_id: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbLoadoutMount {
    pub mount: StdbString,
    pub item_id: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbPlayerLoadout {
    pub account_id: u64,
    pub cart: StdbString,
    pub cart_mounts: *const StdbLoadoutMount,
    pub cart_mounts_count: usize,
    pub smokable: StdbString,
    pub drinkable: StdbString,
    pub bag: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbPlayerBag {
    pub account_id: u64,
    pub room_id: u64,
    /// bag_where in game/bag_place.h.
    pub place: u8,
    /// cart_seat.
    pub seat: u8,
    /// Whose cart, 0 their own.
    pub cart_owner: u64,
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub yaw: f32,
    pub club_in_hand: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbGreenFlag {
    pub id: u64,
    pub room_id: u64,
    pub hole: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbRunningEffect {
    pub id: u64,
    pub account_id: u64,
    pub item_id: StdbString,
    pub target: StdbString,
    /// effect_bucket in game/effects.h.
    pub bucket: u8,
    pub value: f32,
    /// effect_duration.
    pub duration: u8,
    pub seconds: f32,
    /// UTC milliseconds.
    pub started_at_ms: i64,
    pub ends_at_ms: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbLinkCode {
    pub code: StdbString,
    pub expires_at_micros: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbLinkStatus {
    pub result: StdbString,
    pub at_micros: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbServerProtocol {
    /// protocol_version in game/net_types.h.
    pub version: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbServerContent {
    pub hash: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union StdbRow {
    pub player: StdbPlayer,
    pub golfer_skill: StdbGolferSkill,
    pub golfer_progress: StdbGolferProgress,
    pub completed_course: StdbCompletedCourse,
    pub collected: StdbCollected,
    pub world_flag: StdbWorldFlag,
    pub room: StdbRoom,
    pub room_member: StdbRoomMember,
    pub golf_group: StdbGolfGroup,
    pub avatar_motion: StdbAvatarMotion,
    pub ball: StdbBall,
    pub shot_event: StdbShotEvent,
    pub item_use_event: StdbItemUseEvent,
    pub link_code: StdbLinkCode,
    pub link_status: StdbLinkStatus,
    pub server_protocol: StdbServerProtocol,
    pub server_content: StdbServerContent,
    pub golf_cart: StdbGolfCart,
    pub player_loadout: StdbPlayerLoadout,
    pub running_effect: StdbRunningEffect,
    pub player_bag: StdbPlayerBag,
    pub green_flag: StdbGreenFlag,
    pub ride_motion: StdbRideMotion,
}

/// One thing that happened. Only the fields named for its kind are set.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbEvent {
    pub kind: StdbEventKind,
    /// LoginFailed and Disconnected: why, as "id: detail" (the ids are in
    /// stdb_bridge.h). SubscriptionFailed: why. ReducerFailed: the server's
    /// error id. Connected: the identity (hex).
    pub text: StdbString,
    /// ReducerFailed: which reducer.
    pub reducer: StdbString,
    /// SubscriptionApplied, SubscriptionFailed: the stdb_subscribe handle.
    pub subscription: u32,
    /// SignedIn.
    pub login_method: StdbLoginMethod,
    /// Disconnected: the bridge is signing in again by itself.
    pub retrying: bool,
    /// Row.
    pub table: StdbTable,
    pub change: StdbRowChange,
    pub row: StdbRow,
}

/// Every struct's size and every field's offset, in the order
/// `stdb_bridge.h`'s `stdb_layout` check lists them.
pub fn layout() -> Vec<usize> {
    use std::mem::{offset_of, size_of};
    macro_rules! describe {
        ($out:ident, $type:ty, $($field:ident),*) => {{
            $out.push(size_of::<$type>());
            $($out.push(offset_of!($type, $field));)*
        }};
    }
    let mut out = Vec::new();
    describe!(out, StdbString, data, len);
    describe!(out, StdbConfig, server_uri, database, auth_issuer, auth_client_id, auth_scopes, auth_authorization_endpoint,
              auth_token_endpoint, page_signed_in, page_failed, anonymous);
    describe!(out, StdbPlayer, account_id, display_name, online, hand, natural_shape, favourite_colour);
    describe!(out, StdbGolferSkill, id, golfer_id, skill_id, xp);
    describe!(out, StdbGolferProgress, golfer_id, account_id, scope_id, holes_completed);
    describe!(out, StdbCompletedCourse, id, golfer_id, course_id);
    describe!(out, StdbCollected, id, golfer_id, collectible_id, repeatable, claim_count, claimed_at_holes_completed);
    describe!(out, StdbWorldFlag, id, golfer_id, flag);
    describe!(out, StdbRoom, room_id, course_id, player_count);
    describe!(out, StdbRoomMember, account_id, room_id, group_id, round_strokes, round_strokes_count);
    describe!(out, StdbGolfGroup, group_id, room_id, leader, member_count);
    describe!(out, StdbSetup, aim, face_open, ball_position, backswing);
    describe!(out, StdbMotion, mode, x, y, z, yaw, speed, turn_rate, client_time, setup, crouched);
    describe!(out, StdbAvatarMotion, account_id, room_id, motion, server_time_micros);
    describe!(out, StdbRideMotion, rider, owner, motion, server_time_micros);
    describe!(out, StdbBall, account_id, room_id, hole, x, y, z, stroke_count, last_shot_at, in_cup);
    describe!(out, StdbGolfCart, owner, room_id, x, y, z, yaw, passenger, condition, driver, motion_id);
    describe!(out, StdbShot, stroke, aim_angle, face_open, ball_position, backswing, strike_timing, club_id, shot_at, flags_out, flags_out_count);
    describe!(out, StdbMoveRun, kind, steps, forward, side, run, jump, yaw, steer);
    describe!(out, StdbSteps, first_step, start_x, start_y, start_z, runs, run_count);
    describe!(out, StdbSnapshotEntry, kind, x, y, z, yaw, shooter);
    describe!(out, StdbSkillLevel, skill_id, level);
    describe!(out, StdbShotEvent, room_id, account_id, hole, shot, hand, natural_shape, spread_scale, levels, levels_count, seed,
              snapshot, snapshot_count,
              start_x, start_y, start_z, rest_x, rest_y, rest_z, holed);
    describe!(out, StdbItemUseEvent, room_id, account_id, item_id);
    describe!(out, StdbLoadoutMount, mount, item_id);
    describe!(out, StdbPlayerLoadout, account_id, cart, cart_mounts, cart_mounts_count, smokable, drinkable, bag);
    describe!(out, StdbPlayerBag, account_id, room_id, place, seat, cart_owner, x, y, z, yaw, club_in_hand);
    describe!(out, StdbGreenFlag, id, room_id, hole);
    describe!(out, StdbRunningEffect, id, account_id, item_id, target, bucket, value, duration, seconds, started_at_ms, ends_at_ms);
    describe!(out, StdbLinkCode, code, expires_at_micros);
    describe!(out, StdbLinkStatus, result, at_micros);
    describe!(out, StdbServerProtocol, version);
    describe!(out, StdbServerContent, hash);
    out.push(size_of::<StdbRow>());
    describe!(out, StdbEvent, kind, text, reducer, subscription, login_method, retrying, table, change, row);
    out.push(size_of::<StdbEventKind>());
    out.push(size_of::<bool>());
    out
}
