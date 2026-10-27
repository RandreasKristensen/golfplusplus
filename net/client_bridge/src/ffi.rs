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
    PlayerSkill = 1,
    HoleScore = 2,
    CompletedCourse = 3,
    Collected = 4,
    WorldFlag = 5,
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
    pub holes_completed: i32,
    /// handedness in game/shot_setup.h.
    pub hand: u8,
    /// natural_shape in game/golfer.h, or natural_shape_unset.
    pub natural_shape: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbPlayerSkill {
    pub id: u64,
    pub account_id: u64,
    pub skill_id: StdbString,
    pub xp: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbHoleScore {
    pub id: u64,
    pub account_id: u64,
    pub course_id: StdbString,
    pub hole_index: i32,
    pub strokes: i32,
    pub par: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbCompletedCourse {
    pub id: u64,
    pub account_id: u64,
    pub course_id: StdbString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbCollected {
    pub id: u64,
    pub account_id: u64,
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
    pub account_id: u64,
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
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct StdbShot {
    pub stroke: i32,
    pub aim_angle: f32,
    pub face_open: f32,
    pub ball_position: f32,
    pub backswing: f32,
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
pub struct StdbShotEvent {
    pub room_id: u64,
    pub account_id: u64,
    pub hole: i32,
    pub shot: StdbShot,
    pub hand: u8,
    pub natural_shape: u8,
    pub spread_scale: f32,
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
pub union StdbRow {
    pub player: StdbPlayer,
    pub player_skill: StdbPlayerSkill,
    pub hole_score: StdbHoleScore,
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
    pub golf_cart: StdbGolfCart,
    pub player_loadout: StdbPlayerLoadout,
    pub running_effect: StdbRunningEffect,
    pub player_bag: StdbPlayerBag,
    pub green_flag: StdbGreenFlag,
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
    describe!(out, StdbPlayer, account_id, display_name, online, holes_completed, hand, natural_shape);
    describe!(out, StdbPlayerSkill, id, account_id, skill_id, xp);
    describe!(out, StdbHoleScore, id, account_id, course_id, hole_index, strokes, par);
    describe!(out, StdbCompletedCourse, id, account_id, course_id);
    describe!(out, StdbCollected, id, account_id, collectible_id, repeatable, claim_count, claimed_at_holes_completed);
    describe!(out, StdbWorldFlag, id, account_id, flag);
    describe!(out, StdbRoom, room_id, course_id, player_count);
    describe!(out, StdbRoomMember, account_id, room_id, group_id, round_strokes, round_strokes_count);
    describe!(out, StdbGolfGroup, group_id, room_id, leader, member_count);
    describe!(out, StdbMotion, mode, x, y, z, yaw, speed, turn_rate, client_time);
    describe!(out, StdbAvatarMotion, account_id, room_id, motion, server_time_micros);
    describe!(out, StdbBall, account_id, room_id, hole, x, y, z, stroke_count, last_shot_at, in_cup);
    describe!(out, StdbGolfCart, owner, room_id, x, y, z, yaw, passenger, condition);
    describe!(out, StdbShot, stroke, aim_angle, face_open, ball_position, backswing, club_id, shot_at, flags_out, flags_out_count);
    describe!(out, StdbSnapshotEntry, kind, x, y, z, yaw, shooter);
    describe!(out, StdbShotEvent, room_id, account_id, hole, shot, hand, natural_shape, spread_scale, seed, snapshot, snapshot_count,
              start_x, start_y, start_z, rest_x, rest_y, rest_z, holed);
    describe!(out, StdbItemUseEvent, room_id, account_id, item_id);
    describe!(out, StdbLoadoutMount, mount, item_id);
    describe!(out, StdbPlayerLoadout, account_id, cart, cart_mounts, cart_mounts_count, smokable, drinkable, bag);
    describe!(out, StdbPlayerBag, account_id, room_id, place, seat, x, y, z, yaw, club_in_hand);
    describe!(out, StdbGreenFlag, id, room_id, hole);
    describe!(out, StdbRunningEffect, id, account_id, item_id, target, bucket, value, duration, seconds, started_at_ms, ends_at_ms);
    describe!(out, StdbLinkCode, code, expires_at_micros);
    describe!(out, StdbLinkStatus, result, at_micros);
    describe!(out, StdbServerProtocol, version);
    out.push(size_of::<StdbRow>());
    describe!(out, StdbEvent, kind, text, reducer, subscription, login_method, retrying, table, change, row);
    out.push(size_of::<StdbEventKind>());
    out.push(size_of::<bool>());
    out
}
