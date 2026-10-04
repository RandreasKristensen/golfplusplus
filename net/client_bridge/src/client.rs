//! The bridge's state: signing in, opening the connection, its callbacks and
//! subscriptions, and calling reducers. Only ever used from the game's main
//! thread; signing in and opening the WebSocket run on their own threads and
//! hand their results over through channels.

use crate::events::{push, Arena, Event, EventQueue, Row};
use crate::ffi::{StdbLoginMethod, StdbRowChange};
use crate::login::{self, AuthConfig, LoginJob, LoginMessage};
use crate::module_bindings::*;
use spacetimedb_sdk::__codegen::InternalError;
use spacetimedb_sdk::{DbContext, EventTable, SubscriptionHandle as _, Table, TableWithPrimaryKey};
use std::collections::{HashMap, VecDeque};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

/// A lost connection signs in again silently only after it had stayed up
/// this long, so a server that keeps dropping it cannot cause a loop.
const STABLE_CONNECTION: Duration = Duration::from_secs(30);

// Why a connection, subscription or request failed: the id part of a
// LoginFailed, Disconnected, SubscriptionFailed or ReducerFailed reason
// ("id: detail").
const FAILED_CONNECT: &str = "connect_failed";
const FAILED_REJECTED: &str = "connection_rejected";
const LOST_CONNECTION: &str = "connection_lost";
const SUBSCRIPTION_FAILED: &str = "subscription_failed";
const REQUEST_FAILED: &str = "request_failed";
const NOT_CONNECTED: &str = "not_connected";

pub type ReducerDone = Box<dyn FnOnce(&ReducerEventContext, Result<Result<(), String>, InternalError>) + Send>;

/// What the connection's callbacks report to the main thread.
#[derive(Default)]
struct ConnectionState {
    connected: AtomicBool,
    lost: AtomicBool,
    reason: Mutex<String>,
}

impl ConnectionState {
    fn lose(&self, reason: String) {
        if let Ok(mut stored) = self.reason.lock() {
            *stored = reason;
        }
        self.lost.store(true, Ordering::SeqCst);
    }
}

/// A connection being opened on its own thread.
struct Connecting {
    result: Receiver<Result<DbConnection, String>>,
    cancel: Arc<AtomicBool>,
}

impl Drop for Connecting {
    fn drop(&mut self) {
        self.cancel.store(true, Ordering::SeqCst);
    }
}

pub struct StdbClient {
    server_uri: String,
    database: String,
    auth: AuthConfig,
    anonymous: bool,
    pub queue: EventQueue,
    pub arena: Arena,
    login: Option<LoginJob>,
    connecting: Option<Connecting>,
    pub connection: Option<DbConnection>,
    state: Arc<ConnectionState>,
    connected_at: Option<Instant>,
    method: StdbLoginMethod,
    subscriptions: HashMap<u32, SubscriptionHandle>,
    next_subscription: u32,
}

impl StdbClient {
    pub fn new(server_uri: String, database: String, auth: AuthConfig, anonymous: bool) -> StdbClient {
        StdbClient {
            server_uri,
            database,
            auth,
            anonymous,
            queue: Arc::new(Mutex::new(VecDeque::new())),
            arena: Arena::default(),
            login: None,
            connecting: None,
            connection: None,
            state: Arc::new(ConnectionState::default()),
            connected_at: None,
            method: if anonymous { StdbLoginMethod::Anonymous } else { StdbLoginMethod::Browser },
            subscriptions: HashMap::new(),
            next_subscription: 1,
        }
    }

    /// Starts signing in, without the browser when `silent_only`; false when
    /// signing in or connected already.
    pub fn begin_login(&mut self, silent_only: bool) -> bool {
        if self.login.is_some() || self.connecting.is_some() || self.connection.is_some() {
            return false;
        }
        if self.anonymous {
            push(&self.queue, Event::SignedIn(StdbLoginMethod::Anonymous));
            self.start_connecting(None);
        } else {
            self.login = Some(login::start(self.auth.clone(), silent_only));
        }
        true
    }

    pub fn cancel_login(&mut self) {
        self.login = None;
        self.connecting = None;
    }

    pub fn sign_out(&mut self) {
        self.cancel_login();
        self.close();
        login::forget_refresh_token(&self.auth);
    }

    fn start_connecting(&mut self, token: Option<String>) {
        let (sender, result) = channel();
        let cancel = Arc::new(AtomicBool::new(false));
        let thread_cancel = cancel.clone();
        let state = Arc::new(ConnectionState::default());
        self.state = state.clone();
        let (uri, database, queue) = (self.server_uri.clone(), self.database.clone(), self.queue.clone());
        thread::spawn(move || {
            let (connect_state, error_state, lost_state) = (state.clone(), state.clone(), state);
            let built = DbConnection::builder()
                .with_uri(uri)
                .with_database_name(database)
                .with_token(token)
                .on_connect(move |_, identity, _| {
                    connect_state.connected.store(true, Ordering::SeqCst);
                    push(&queue, Event::Connected(identity.to_hex().to_string()));
                })
                .on_connect_error(move |_, error| error_state.lose(error.to_string()))
                .on_disconnect(move |_, error| lost_state.lose(error.map(|e| e.to_string()).unwrap_or_default()))
                .build()
                .map_err(|e| e.to_string());
            match built {
                // Cancelled while opening: nobody will take it.
                Ok(conn) if thread_cancel.load(Ordering::SeqCst) => {
                    let _ = conn.disconnect();
                }
                built => {
                    if let Err(Ok(conn)) = sender.send(built).map_err(|unsent| unsent.0) {
                        let _ = conn.disconnect();
                    }
                }
            }
        });
        self.connecting = Some(Connecting { result, cancel });
    }

    fn register_callbacks(&self, conn: &DbConnection) {
        macro_rules! watch {
            ($table:ident, $variant:ident) => {{
                let q = self.queue.clone();
                conn.db.$table().on_insert(move |_, row| push(&q, Event::Row(StdbRowChange::Insert, Row::$variant(row.clone()))));
                let q = self.queue.clone();
                conn.db.$table().on_delete(move |_, row| push(&q, Event::Row(StdbRowChange::Delete, Row::$variant(row.clone()))));
            }};
        }
        macro_rules! watch_keyed {
            ($table:ident, $variant:ident) => {{
                watch!($table, $variant);
                let q = self.queue.clone();
                conn.db.$table().on_update(move |_, _, row| push(&q, Event::Row(StdbRowChange::Update, Row::$variant(row.clone()))));
            }};
        }
        macro_rules! watch_events {
            ($table:ident, $variant:ident) => {{
                let q = self.queue.clone();
                conn.db.$table().on_insert(move |_, row| push(&q, Event::Row(StdbRowChange::Insert, Row::$variant(row.clone()))));
            }};
        }
        watch_keyed!(player, Player);
        watch_keyed!(player_skill, PlayerSkill);
        watch_keyed!(hole_score, HoleScore);
        watch_keyed!(completed_course, CompletedCourse);
        watch_keyed!(collected, Collected);
        watch_keyed!(world_flag, WorldFlag);
        watch_keyed!(room, Room);
        watch_keyed!(room_member, RoomMember);
        watch_keyed!(golf_group, GolfGroup);
        watch_keyed!(avatar_motion, AvatarMotion);
        watch_keyed!(ball, Ball);
        watch_events!(shot_event, ShotEvent);
        watch_events!(emote_event, EmoteEvent);
        watch!(my_account, MyAccount);
        watch!(my_link_code, MyLinkCode);
        watch!(my_link_status, MyLinkStatus);
    }

    /// Closes the connection the game asked to close.
    fn close(&mut self) {
        self.subscriptions.clear();
        self.connected_at = None;
        if let Some(conn) = self.connection.take() {
            let _ = conn.disconnect();
            push(&self.queue, Event::Disconnected(String::new(), false));
        }
    }

    pub fn tick(&mut self) {
        if let Some(job) = &self.login {
            match job.messages.try_recv() {
                Ok(LoginMessage::WaitingForBrowser) => push(&self.queue, Event::LoginWaitingForBrowser),
                Ok(LoginMessage::Token(token)) => {
                    self.login = None;
                    self.method = StdbLoginMethod::Browser;
                    push(&self.queue, Event::SignedIn(StdbLoginMethod::Browser));
                    self.start_connecting(Some(token));
                }
                Ok(LoginMessage::Failed(reason)) => {
                    self.login = None;
                    push(&self.queue, Event::LoginFailed(reason));
                }
                Err(_) => {}
            }
        }

        if let Some(connecting) = &self.connecting {
            if let Ok(result) = connecting.result.try_recv() {
                self.connecting = None;
                match result {
                    Ok(conn) => {
                        self.register_callbacks(&conn);
                        self.connection = Some(conn);
                    }
                    Err(reason) => push(&self.queue, Event::LoginFailed(login::failure(FAILED_CONNECT, reason))),
                }
            }
        }

        if let Some(conn) = &self.connection {
            if let Err(error) = conn.frame_tick() {
                self.state.lose(error.to_string());
            }
            if self.connected_at.is_none() && self.state.connected.load(Ordering::SeqCst) {
                self.connected_at = Some(Instant::now());
            }
        }
        if self.connection.is_some() && self.state.lost.load(Ordering::SeqCst) {
            self.handle_lost_connection();
        }
    }

    fn handle_lost_connection(&mut self) {
        let reason = self.state.reason.lock().map(|r| r.clone()).unwrap_or_default();
        let was_connected = self.state.connected.load(Ordering::SeqCst);
        let stable = self.connected_at.is_some_and(|at| at.elapsed() >= STABLE_CONNECTION);
        self.subscriptions.clear();
        self.connected_at = None;
        if let Some(conn) = self.connection.take() {
            let _ = conn.disconnect();
        }
        if !was_connected {
            // The server refused the connection (a bad token, say).
            push(&self.queue, Event::LoginFailed(login::failure(FAILED_REJECTED, reason)));
            return;
        }
        // A lost browser sign-in (an expired token, say) signs in again
        // silently; if that fails, LoginFailed follows.
        let retrying = self.method == StdbLoginMethod::Browser && stable;
        push(&self.queue, Event::Disconnected(login::failure(LOST_CONNECTION, reason), retrying));
        if retrying {
            self.login = Some(login::start(self.auth.clone(), true));
        }
    }

    pub fn subscribe(&mut self, sql: Vec<String>) -> u32 {
        let Some(conn) = &self.connection else { return 0 };
        let id = self.next_subscription;
        self.next_subscription += 1;
        let (applied, failed) = (self.queue.clone(), self.queue.clone());
        let handle = conn
            .subscription_builder()
            .on_applied(move |_| push(&applied, Event::SubscriptionApplied(id)))
            .on_error(move |_, error| push(&failed, Event::SubscriptionFailed(id, login::failure(SUBSCRIPTION_FAILED, error))))
            .subscribe(sql);
        self.subscriptions.insert(id, handle);
        id
    }

    pub fn unsubscribe(&mut self, id: u32) {
        if let Some(handle) = self.subscriptions.remove(&id) {
            let _ = handle.unsubscribe();
        }
    }

    /// Calls a reducer, reporting a failure (or no connection) as an event.
    pub fn call<F>(&self, reducer: &'static str, invoke: F)
    where
        F: FnOnce(&DbConnection, ReducerDone) -> spacetimedb_sdk::Result<()>,
    {
        let Some(conn) = &self.connection else {
            push(&self.queue, Event::ReducerFailed(reducer, NOT_CONNECTED.to_string()));
            return;
        };
        let queue = self.queue.clone();
        let report: ReducerDone = Box::new(move |_, result| match result {
            Ok(Ok(())) => {}
            Ok(Err(message)) => push(&queue, Event::ReducerFailed(reducer, message)),
            Err(error) => push(&queue, Event::ReducerFailed(reducer, login::failure(REQUEST_FAILED, error))),
        });
        if let Err(error) = invoke(conn, report) {
            push(&self.queue, Event::ReducerFailed(reducer, login::failure(REQUEST_FAILED, error)));
        }
    }
}

impl Drop for StdbClient {
    fn drop(&mut self) {
        self.login = None;
        self.connecting = None;
        if let Some(conn) = self.connection.take() {
            let _ = conn.disconnect();
        }
    }
}
