//! Events as the bridge queues them (owned Rust data), and their conversion
//! to the C structs `stdb_poll` hands out.

use crate::ffi::*;
use crate::module_bindings::*;
use std::collections::VecDeque;
use std::sync::{Arc, Mutex};

pub enum Row {
    Player(PlayerRow),
    PlayerSkill(PlayerSkillRow),
    HoleScore(HoleScoreRow),
    CompletedCourse(CompletedCourseRow),
    Collected(CollectedRow),
    WorldFlag(WorldFlagRow),
    Room(RoomRow),
    RoomMember(RoomMemberRow),
    GolfGroup(GolfGroupRow),
    AvatarMotion(AvatarMotionRow),
    Ball(BallRow),
    ShotEvent(ShotEventRow),
    ItemUseEvent(ItemUseEventRow),
    PlayerLoadout(PlayerLoadoutRow),
    RunningEffect(RunningEffectRow),
    PlayerBag(PlayerBagRow),
    GreenFlag(GreenFlagRow),
    MyAccount(PlayerRow),
    MyLinkCode(LinkCodeView),
    MyLinkStatus(LinkStatus),
    ServerProtocol(ServerProtocolInfo),
    GolfCart(GolfCartRow),
}

pub enum Event {
    LoginWaitingForBrowser,
    LoginFailed(String),
    SignedIn(StdbLoginMethod),
    Connected(String),
    /// Why, and whether the bridge is signing in again.
    Disconnected(String, bool),
    SubscriptionApplied(u32),
    SubscriptionFailed(u32, String),
    Row(StdbRowChange, Row),
    ReducerFailed(&'static str, String),
}

/// Shared by the SDK's callbacks, which all run on the thread that calls
/// `stdb_frame_tick`.
pub type EventQueue = Arc<Mutex<VecDeque<Event>>>;

pub fn push(queue: &EventQueue, event: Event) {
    if let Ok(mut events) = queue.lock() {
        events.push_back(event);
    }
}

/// Owns what the C events of one poll point to.
#[derive(Default)]
pub struct Arena {
    strings: Vec<Box<[u8]>>,
    ints: Vec<Box<[i32]>>,
    mounts: Vec<Box<[StdbLoadoutMount]>>,
    snapshots: Vec<Box<[StdbSnapshotEntry]>>,
}

impl Arena {
    pub fn clear(&mut self) {
        self.strings.clear();
        self.ints.clear();
        self.mounts.clear();
        self.snapshots.clear();
    }

    fn text(&mut self, text: &str) -> StdbString {
        let bytes: Box<[u8]> = text.as_bytes().into();
        let string = StdbString { data: bytes.as_ptr() as *const _, len: bytes.len() };
        self.strings.push(bytes);
        string
    }

    fn mounts(&mut self, mounts: &[LoadoutMount]) -> (*const StdbLoadoutMount, usize) {
        let owned: Box<[StdbLoadoutMount]> =
            mounts.iter().map(|m| StdbLoadoutMount { mount: self.text(&m.mount), item_id: self.text(&m.item_id) }).collect();
        let view = (owned.as_ptr(), owned.len());
        self.mounts.push(owned);
        view
    }

    fn snapshot(&mut self, entries: &[SnapshotRowEntry]) -> (*const StdbSnapshotEntry, usize) {
        let owned: Box<[StdbSnapshotEntry]> = entries
            .iter()
            .map(|e| StdbSnapshotEntry { kind: e.kind, x: e.x, y: e.y, z: e.z, yaw: e.yaw, shooter: e.shooter })
            .collect();
        let view = (owned.as_ptr(), owned.len());
        self.snapshots.push(owned);
        view
    }

    fn ints(&mut self, values: &[i32]) -> (*const i32, usize) {
        let owned: Box<[i32]> = values.into();
        let view = (owned.as_ptr(), owned.len());
        self.ints.push(owned);
        view
    }
}

fn empty_event(kind: StdbEventKind) -> StdbEvent {
    StdbEvent {
        kind,
        text: StdbString::EMPTY,
        reducer: StdbString::EMPTY,
        subscription: 0,
        login_method: StdbLoginMethod::Browser,
        retrying: false,
        table: StdbTable::Player,
        change: StdbRowChange::Insert,
        row: StdbRow {
            ball: StdbBall { account_id: 0, room_id: 0, hole: 0, x: 0.0, y: 0.0, z: 0.0, stroke_count: 0, last_shot_at: 0, in_cup: false },
        },
    }
}

fn player(arena: &mut Arena, row: &PlayerRow) -> StdbPlayer {
    StdbPlayer {
        account_id: row.account_id,
        display_name: arena.text(&row.display_name),
        online: row.online,
        holes_completed: row.holes_completed,
        hand: row.hand,
        natural_shape: row.natural_shape,
    }
}

fn row_to_c(arena: &mut Arena, row: &Row) -> (StdbTable, StdbRow) {
    match row {
        Row::Player(r) => (StdbTable::Player, StdbRow { player: player(arena, r) }),
        Row::MyAccount(r) => (StdbTable::MyAccount, StdbRow { player: player(arena, r) }),
        Row::PlayerSkill(r) => (
            StdbTable::PlayerSkill,
            StdbRow { player_skill: StdbPlayerSkill { id: r.id, account_id: r.account_id, skill_id: arena.text(&r.skill_id), xp: r.xp } },
        ),
        Row::HoleScore(r) => (
            StdbTable::HoleScore,
            StdbRow {
                hole_score: StdbHoleScore {
                    id: r.id,
                    account_id: r.account_id,
                    course_id: arena.text(&r.course_id),
                    hole_index: r.hole_index,
                    strokes: r.strokes,
                    par: r.par,
                },
            },
        ),
        Row::CompletedCourse(r) => (
            StdbTable::CompletedCourse,
            StdbRow { completed_course: StdbCompletedCourse { id: r.id, account_id: r.account_id, course_id: arena.text(&r.course_id) } },
        ),
        Row::Collected(r) => (
            StdbTable::Collected,
            StdbRow {
                collected: StdbCollected {
                    id: r.id,
                    account_id: r.account_id,
                    collectible_id: arena.text(&r.collectible_id),
                    repeatable: r.repeatable,
                    claim_count: r.claim_count,
                    claimed_at_holes_completed: r.claimed_at_holes_completed,
                },
            },
        ),
        Row::WorldFlag(r) => (
            StdbTable::WorldFlag,
            StdbRow { world_flag: StdbWorldFlag { id: r.id, account_id: r.account_id, flag: arena.text(&r.flag) } },
        ),
        Row::Room(r) => (
            StdbTable::Room,
            StdbRow { room: StdbRoom { room_id: r.room_id, course_id: arena.text(&r.course_id), player_count: r.player_count } },
        ),
        Row::RoomMember(r) => {
            let (strokes, count) = arena.ints(&r.round_strokes);
            (
                StdbTable::RoomMember,
                StdbRow {
                    room_member: StdbRoomMember {
                        account_id: r.account_id,
                        room_id: r.room_id,
                        group_id: r.group_id,
                        round_strokes: strokes,
                        round_strokes_count: count,
                    },
                },
            )
        }
        Row::GolfGroup(r) => (
            StdbTable::GolfGroup,
            StdbRow { golf_group: StdbGolfGroup { group_id: r.group_id, room_id: r.room_id, leader: r.leader, member_count: r.member_count } },
        ),
        Row::AvatarMotion(r) => (
            StdbTable::AvatarMotion,
            StdbRow {
                avatar_motion: StdbAvatarMotion {
                    account_id: r.account_id,
                    room_id: r.room_id,
                    motion: StdbMotion {
                        mode: r.mode,
                        x: r.x,
                        y: r.y,
                        z: r.z,
                        yaw: r.yaw,
                        speed: r.speed,
                        turn_rate: r.turn_rate,
                        client_time: r.client_time,
                    },
                    server_time_micros: r.server_time.to_micros_since_unix_epoch(),
                },
            },
        ),
        Row::Ball(r) => (
            StdbTable::Ball,
            StdbRow {
                ball: StdbBall {
                    account_id: r.account_id,
                    room_id: r.room_id,
                    hole: r.hole,
                    x: r.x,
                    y: r.y,
                    z: r.z,
                    stroke_count: r.stroke_count,
                    last_shot_at: r.last_shot_at,
                    in_cup: r.in_cup,
                },
            },
        ),
        Row::ShotEvent(r) => {
            let (flags_out, flags_out_count) = arena.ints(&r.flags_out);
            let (snapshot, snapshot_count) = arena.snapshot(&r.snapshot);
            (
            StdbTable::ShotEvent,
            StdbRow {
                shot_event: StdbShotEvent {
                    room_id: r.room_id,
                    account_id: r.account_id,
                    hole: r.hole,
                    shot: StdbShot {
                        stroke: r.stroke,
                        aim_angle: r.aim_angle,
                        face_open: r.face_open,
                        ball_position: r.ball_position,
                        backswing: r.backswing,
                        club_id: arena.text(&r.club_id),
                        shot_at: r.shot_at,
                        flags_out,
                        flags_out_count,
                    },
                    hand: r.hand,
                    natural_shape: r.natural_shape,
                    spread_scale: r.spread_scale,
                    seed: r.seed,
                    snapshot,
                    snapshot_count,
                    start_x: r.start_x,
                    start_y: r.start_y,
                    start_z: r.start_z,
                    rest_x: r.rest_x,
                    rest_y: r.rest_y,
                    rest_z: r.rest_z,
                    holed: r.holed,
                },
            },
            )
        }
        Row::ItemUseEvent(r) => (
            StdbTable::ItemUseEvent,
            StdbRow {
                item_use_event: StdbItemUseEvent { room_id: r.room_id, account_id: r.account_id, item_id: arena.text(&r.item_id) },
            },
        ),
        Row::PlayerLoadout(r) => {
            let (mounts, count) = arena.mounts(&r.cart_mounts);
            (
                StdbTable::PlayerLoadout,
                StdbRow {
                    player_loadout: StdbPlayerLoadout {
                        account_id: r.account_id,
                        cart: arena.text(&r.cart),
                        cart_mounts: mounts,
                        cart_mounts_count: count,
                        smokable: arena.text(&r.smokable),
                        drinkable: arena.text(&r.drinkable),
                        bag: arena.text(&r.bag),
                    },
                },
            )
        }
        Row::PlayerBag(r) => (
            StdbTable::PlayerBag,
            StdbRow {
                player_bag: StdbPlayerBag {
                    account_id: r.account_id,
                    room_id: r.room_id,
                    place: r.place,
                    seat: r.seat,
                    x: r.x,
                    y: r.y,
                    z: r.z,
                    yaw: r.yaw,
                    club_in_hand: arena.text(&r.club_in_hand),
                },
            },
        ),
        Row::GreenFlag(r) => (
            StdbTable::GreenFlag,
            StdbRow { green_flag: StdbGreenFlag { id: r.id, room_id: r.room_id, hole: r.hole } },
        ),
        Row::RunningEffect(r) => (
            StdbTable::RunningEffect,
            StdbRow {
                running_effect: StdbRunningEffect {
                    id: r.id,
                    account_id: r.account_id,
                    item_id: arena.text(&r.item_id),
                    target: arena.text(&r.target),
                    bucket: r.bucket,
                    value: r.value,
                    duration: r.duration,
                    seconds: r.seconds,
                    started_at_ms: r.started_at_ms,
                    ends_at_ms: r.ends_at_ms,
                },
            },
        ),
        Row::MyLinkCode(r) => (
            StdbTable::MyLinkCode,
            StdbRow { link_code: StdbLinkCode { code: arena.text(&r.code), expires_at_micros: r.expires_at.to_micros_since_unix_epoch() } },
        ),
        Row::MyLinkStatus(r) => (
            StdbTable::MyLinkStatus,
            StdbRow { link_status: StdbLinkStatus { result: arena.text(&r.result), at_micros: r.at.to_micros_since_unix_epoch() } },
        ),
        Row::ServerProtocol(r) => (StdbTable::ServerProtocol, StdbRow { server_protocol: StdbServerProtocol { version: r.version } }),
        Row::GolfCart(r) => (
            StdbTable::GolfCart,
            StdbRow {
                golf_cart: StdbGolfCart {
                    owner: r.owner,
                    room_id: r.room_id,
                    x: r.x,
                    y: r.y,
                    z: r.z,
                    yaw: r.yaw,
                    passenger: r.passenger,
                    condition: r.condition,
                },
            },
        ),
    }
}

pub fn to_c(arena: &mut Arena, event: &Event) -> StdbEvent {
    match event {
        Event::LoginWaitingForBrowser => empty_event(StdbEventKind::LoginWaitingForBrowser),
        Event::LoginFailed(reason) => StdbEvent { text: arena.text(reason), ..empty_event(StdbEventKind::LoginFailed) },
        Event::SignedIn(method) => StdbEvent { login_method: *method, ..empty_event(StdbEventKind::SignedIn) },
        Event::Connected(identity) => StdbEvent { text: arena.text(identity), ..empty_event(StdbEventKind::Connected) },
        Event::Disconnected(reason, retrying) => StdbEvent {
            text: arena.text(reason),
            retrying: *retrying,
            ..empty_event(StdbEventKind::Disconnected)
        },
        Event::SubscriptionApplied(id) => StdbEvent { subscription: *id, ..empty_event(StdbEventKind::SubscriptionApplied) },
        Event::SubscriptionFailed(id, reason) => StdbEvent {
            subscription: *id,
            text: arena.text(reason),
            ..empty_event(StdbEventKind::SubscriptionFailed)
        },
        Event::Row(change, row) => {
            let (table, row) = row_to_c(arena, row);
            StdbEvent { table, change: *change, row, ..empty_event(StdbEventKind::Row) }
        }
        Event::ReducerFailed(reducer, message) => StdbEvent {
            reducer: arena.text(reducer),
            text: arena.text(message),
            ..empty_event(StdbEventKind::ReducerFailed)
        },
    }
}
