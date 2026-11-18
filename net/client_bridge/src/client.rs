//! The bridge's state: signing in, opening the connection, its callbacks and
//! subscriptions, and calling reducers. Only ever used from the game's main
//! thread; signing in and opening the WebSocket run on their own threads and
//! hand their results over through channels. The current attempt alone owns
//! the event queue and the stored sign-in: a cancelled one's late events go
//! to a retired queue and its credential change is never applied.

use crate::events::{push, Arena, Event, EventQueue, Row};
use crate::ffi::{StdbLoginMethod, StdbRowChange};
use crate::login::{self, AuthConfig, CredentialStore, LoginJob, LoginMessage};
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
        // Opened but not yet taken: nobody else will close it.
        if let Ok(Ok(conn)) = self.result.try_recv() {
            let _ = conn.disconnect();
        }
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
    store: Box<dyn CredentialStore>,
}

impl StdbClient {
    pub fn new(
        server_uri: String,
        database: String,
        auth: AuthConfig,
        anonymous: bool,
        store: Box<dyn CredentialStore>,
    ) -> StdbClient {
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
            store,
        }
    }

    /// Starts signing in, without the browser when `silent_only`; false when
    /// signing in or connected already.
    pub fn begin_login(&mut self, silent_only: bool) -> bool {
        if self.login.is_some() || self.connecting.is_some() || self.connection.is_some() {
            return false;
        }
        // Nothing of an earlier attempt or session may reach this one.
        self.queue = Arc::new(Mutex::new(VecDeque::new()));
        if self.anonymous {
            push(&self.queue, Event::SignedIn(StdbLoginMethod::Anonymous));
            self.start_connecting(None);
        } else {
            self.start_login(silent_only);
        }
        true
    }

    fn start_login(&mut self, silent_only: bool) {
        self.login = Some(login::start(self.auth.clone(), self.store.load(), silent_only));
    }

    /// Abandons signing in and any connection the game has not accepted,
    /// keeping the stored sign-in.
    pub fn cancel_login(&mut self) {
        self.retire();
    }

    pub fn disconnect(&mut self) {
        if self.retire() {
            push(&self.queue, Event::Disconnected(String::new(), false));
        }
    }

    /// A guest's sign-out keeps the browser's stored sign-in.
    pub fn sign_out(&mut self) {
        self.disconnect();
        if !self.anonymous {
            self.store.forget();
        }
    }

    /// Ends the current attempt or session: its sign-in and connecting jobs
    /// (whose results are then never applied), its subscriptions and its
    /// connection, and retires the events queued or still to come from them.
    /// True when there was a connection.
    fn retire(&mut self) -> bool {
        self.login = None;
        self.connecting = None;
        self.subscriptions.clear();
        self.connected_at = None;
        self.queue = Arc::new(Mutex::new(VecDeque::new()));
        let connection = self.connection.take();
        if let Some(conn) = &connection {
            let _ = conn.disconnect();
        }
        connection.is_some()
    }

    fn start_connecting(&mut self, token: Option<String>) {
        let lifecycle = self.queue.lock().map(|mut queue| queue.drain(..).filter(|event|
            matches!(event, Event::SignedIn(_) | Event::Disconnected(_, _) | Event::LoginWaitingForBrowser |
                           Event::LoginFailed(_))).collect()).unwrap_or_default();
        self.queue = Arc::new(Mutex::new(lifecycle));
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
                conn.db.$table().on_delete(move |ctx, row| {
                    if !matches!(ctx.event, spacetimedb_sdk::Event::UnsubscribeApplied | spacetimedb_sdk::Event::Disconnected) {
                        push(&q, Event::Row(StdbRowChange::Delete, Row::$variant(row.clone())));
                    }
                });
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
        watch!(my_tee_slots, TeeSlot);
        watch!(departure_board, DepartureBoard);
        watch!(my_tee_reservation, MyTeeReservation);
        watch!(my_tee_book_status, MyTeeBookStatus);
        watch_keyed!(room, Room);
        watch_keyed!(room_member, RoomMember);
        watch_keyed!(golf_group, GolfGroup);
        watch!(my_avatar_motion, AvatarMotion);
        watch_keyed!(ball, Ball);
        watch_keyed!(practice_ball, PracticeBall);
        watch_keyed!(dap_pair, Dap);
        watch!(my_golf_carts, GolfCart);
        watch_keyed!(player_loadout, PlayerLoadout);
        watch_keyed!(running_effect, RunningEffect);
        watch_keyed!(player_bag, PlayerBag);
        watch_keyed!(green_flag, GreenFlag);
        watch_keyed!(ride_motion, RideMotion);
        watch_events!(shot_event, ShotEvent);
        watch_events!(item_use_event, ItemUseEvent);
        watch!(my_account, MyAccount);
        watch!(my_golfer_progress, MyGolferProgress);
        watch!(my_skills, MySkills);
        watch!(my_completed_courses, MyCompletedCourses);
        watch!(my_collected, MyCollected);
        watch!(my_world_flags, MyWorldFlags);
        watch!(my_link_code, MyLinkCode);
        watch!(my_link_status, MyLinkStatus);
        watch!(server_protocol, ServerProtocol);
        watch!(server_content_hash, ServerContentHash);
    }

    pub fn tick(&mut self) {
        if let Some(job) = &self.login {
            match job.messages.try_recv() {
                Ok(LoginMessage::WaitingForBrowser) => push(&self.queue, Event::LoginWaitingForBrowser),
                Ok(LoginMessage::Done(token, credential)) => {
                    self.login = None;
                    login::apply(self.store.as_ref(), credential);
                    match token {
                        Ok(token) => {
                            self.method = StdbLoginMethod::Browser;
                            push(&self.queue, Event::SignedIn(StdbLoginMethod::Browser));
                            self.start_connecting(Some(token));
                        }
                        Err(reason) => push(&self.queue, Event::LoginFailed(reason)),
                    }
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
            self.start_login(true);
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
    pub fn call<F>(&self, reducer: &'static str, invoke: F) -> bool
    where
        F: FnOnce(&DbConnection, ReducerDone) -> spacetimedb_sdk::Result<()>,
    {
        let Some(conn) = &self.connection else {
            push(&self.queue, Event::ReducerFailed(reducer, NOT_CONNECTED.to_string()));
            return false;
        };
        let queue = self.queue.clone();
        let report: ReducerDone = Box::new(move |_, result| match result {
            Ok(Ok(())) => {}
            Ok(Err(message)) => push(&queue, Event::ReducerFailed(reducer, message)),
            Err(error) => push(&queue, Event::ReducerFailed(reducer, login::failure(REQUEST_FAILED, error))),
        });
        if let Err(error) = invoke(conn, report) {
            push(&self.queue, Event::ReducerFailed(reducer, login::failure(REQUEST_FAILED, error)));
            return false;
        }
        true
    }
}

impl Drop for StdbClient {
    fn drop(&mut self) {
        self.retire();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::{TcpListener, TcpStream};
    use std::thread::JoinHandle;

    #[derive(Default)]
    struct Stored {
        token: Option<String>,
        saves: usize,
    }

    /// The credential store in memory, shared with the test.
    #[derive(Clone, Default)]
    struct MemoryStore(Arc<Mutex<Stored>>);

    impl MemoryStore {
        fn holding(token: &str) -> MemoryStore {
            let store = MemoryStore::default();
            store.0.lock().unwrap().token = Some(token.to_string());
            store
        }
        fn token(&self) -> Option<String> {
            self.0.lock().unwrap().token.clone()
        }
        fn saves(&self) -> usize {
            self.0.lock().unwrap().saves
        }
    }

    impl CredentialStore for MemoryStore {
        fn load(&self) -> Result<String, String> {
            self.token().ok_or_else(|| "no entry".to_string())
        }
        fn save(&self, refresh_token: &str) {
            let mut stored = self.0.lock().unwrap();
            stored.token = Some(refresh_token.to_string());
            stored.saves += 1;
        }
        fn forget(&self) {
            self.0.lock().unwrap().token = None;
        }
    }

    const ROTATED: &str = r#"{"access_token":"a","token_type":"bearer","id_token":"id","refresh_token":"rotated"}"#;
    const REFUSED: &str = r#"{"error":"invalid_grant"}"#;
    const SERVER_ERROR: &str = r#"{"error":"temporarily_unavailable"}"#;

    /// A token endpoint whose requests wait until the test answers them, and
    /// a game server that accepts connections and never answers.
    struct Endpoints {
        token: TcpListener,
        server: TcpListener,
    }

    impl Endpoints {
        fn new() -> Endpoints {
            Endpoints { token: TcpListener::bind("127.0.0.1:0").unwrap(), server: TcpListener::bind("127.0.0.1:0").unwrap() }
        }

        fn client(&self, store: &MemoryStore, anonymous: bool) -> StdbClient {
            let auth = AuthConfig {
                issuer: "https://issuer.invalid".into(),
                client_id: "golfpp-test".into(),
                scopes: String::new(),
                authorization_endpoint: "https://issuer.invalid/authorize".into(),
                token_endpoint: format!("http://{}/token", self.token.local_addr().unwrap()),
                page_signed_in: String::new(),
                page_failed: String::new(),
            };
            let server = format!("http://{}", self.server.local_addr().unwrap());
            StdbClient::new(server, "golfpp".into(), auth, anonymous, Box::new(store.clone()))
        }

        /// The next token request, read whole; its sign-in waits for the answer.
        fn request(&self) -> (TcpStream, String) {
            let (mut stream, _) = self.token.accept().unwrap();
            let mut buffer = Vec::new();
            let mut chunk = [0u8; 1024];
            loop {
                let text = String::from_utf8_lossy(&buffer).into_owned();
                if let Some(end) = text.find("\r\n\r\n") {
                    let length = text[..end]
                        .lines()
                        .find_map(|line| line.to_ascii_lowercase().strip_prefix("content-length:").map(|n| n.trim().parse().unwrap()))
                        .unwrap_or(0usize);
                    if buffer.len() >= end + 4 + length {
                        return (stream, text);
                    }
                }
                let read = stream.read(&mut chunk).unwrap();
                assert!(read > 0, "the token request ended early");
                buffer.extend_from_slice(&chunk[..read]);
            }
        }
    }

    fn answer(mut stream: TcpStream, status: &str, body: &str) {
        let reply = format!(
            "HTTP/1.1 {status}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
            body.len()
        );
        stream.write_all(reply.as_bytes()).unwrap();
    }

    /// The current sign-in's thread, to join once its answer is released.
    fn worker(client: &mut StdbClient) -> JoinHandle<()> {
        client.login.as_mut().unwrap().worker.take().unwrap()
    }

    /// Lets the current sign-in finish, then accepts its result.
    fn finish(client: &mut StdbClient) {
        worker(client).join().unwrap();
        client.tick();
    }

    fn queued(client: &StdbClient) -> Vec<String> {
        client
            .queue
            .lock()
            .unwrap()
            .iter()
            .map(|event| match event {
                Event::SignedIn(_) => "signed_in".to_string(),
                Event::LoginFailed(reason) => format!("failed {reason}"),
                Event::Connected(_) => "connected".to_string(),
                Event::Disconnected(_, _) => "disconnected".to_string(),
                _ => "other".to_string(),
            })
            .collect()
    }

    #[test]
    fn a_rotated_refresh_token_is_stored_once_by_the_owner() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        let mut client = endpoints.client(&store, false);
        assert!(client.begin_login(true));
        let (stream, request) = endpoints.request();
        assert!(request.contains("refresh_token=stored"));
        answer(stream, "200 OK", ROTATED);
        worker(&mut client).join().unwrap();
        assert_eq!(store.token().as_deref(), Some("stored"));
        client.tick();
        assert_eq!(store.token().as_deref(), Some("rotated"));
        assert_eq!(store.saves(), 1);
        assert_eq!(queued(&client), ["signed_in"]);
        client.tick();
        assert_eq!(store.saves(), 1);
    }

    #[test]
    fn a_refresh_answered_after_sign_out_cannot_restore_the_token() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        let mut client = endpoints.client(&store, false);
        assert!(client.begin_login(true));
        let (stream, _) = endpoints.request();
        let worker = worker(&mut client);
        client.sign_out();
        assert_eq!(store.token(), None);
        answer(stream, "200 OK", ROTATED);
        worker.join().unwrap();
        client.tick();
        assert_eq!(store.token(), None);
        assert_eq!(store.saves(), 0);
        assert!(queued(&client).is_empty());

        assert!(client.begin_login(true));
        finish(&mut client);
        let events = queued(&client);
        assert_eq!(events.len(), 1);
        assert!(events[0].starts_with(&format!("failed {}", login::FAILED_NO_STORED_SIGN_IN)));
    }

    #[test]
    fn an_old_refusal_cannot_forget_a_newer_sign_in() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        let mut client = endpoints.client(&store, false);
        assert!(client.begin_login(true));
        let (old, _) = endpoints.request();
        let old_worker = worker(&mut client);
        client.cancel_login();
        assert_eq!(store.token().as_deref(), Some("stored"));

        assert!(client.begin_login(true));
        let (new, _) = endpoints.request();
        answer(new, "200 OK", ROTATED);
        finish(&mut client);
        assert_eq!(store.token().as_deref(), Some("rotated"));

        answer(old, "400 Bad Request", REFUSED);
        old_worker.join().unwrap();
        client.tick();
        assert_eq!(store.token().as_deref(), Some("rotated"));
    }

    #[test]
    fn only_a_refused_refresh_token_is_forgotten() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        let mut client = endpoints.client(&store, false);
        assert!(client.begin_login(true));
        let (stream, _) = endpoints.request();
        answer(stream, "503 Service Unavailable", SERVER_ERROR);
        finish(&mut client);
        assert_eq!(store.token().as_deref(), Some("stored"));

        assert!(client.begin_login(true));
        let (stream, _) = endpoints.request();
        answer(stream, "400 Bad Request", REFUSED);
        finish(&mut client);
        assert_eq!(store.token(), None);
        assert!(queued(&client).iter().all(|event| event.starts_with("failed")));

        // A refusal of a token another has replaced since leaves the new one.
        let store = MemoryStore::holding("newer");
        login::apply(&store, login::CredentialChange::Forget("stored".into()));
        assert_eq!(store.token().as_deref(), Some("newer"));
    }

    #[test]
    fn cancelling_while_connecting_retires_the_attempt_and_its_late_events() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        let mut client = endpoints.client(&store, false);
        assert!(client.begin_login(true));
        let (stream, _) = endpoints.request();
        answer(stream, "200 OK", ROTATED);
        finish(&mut client);
        assert!(client.connecting.is_some());
        assert_eq!(queued(&client), ["signed_in"]);

        let attempt_queue = client.queue.clone();
        client.cancel_login();
        push(&attempt_queue, Event::Connected("late".into()));
        client.tick();
        assert!(client.connecting.is_none() && client.connection.is_none() && client.login.is_none());
        assert!(queued(&client).is_empty());
        assert_eq!(store.token().as_deref(), Some("rotated"));
        assert!(client.begin_login(true));
    }

    #[test]
    fn a_guest_sign_out_keeps_the_browser_sign_in_and_a_named_one_forgets_it() {
        let endpoints = Endpoints::new();
        let store = MemoryStore::holding("stored");
        endpoints.client(&store, true).sign_out();
        assert_eq!(store.token().as_deref(), Some("stored"));
        endpoints.client(&store, false).sign_out();
        assert_eq!(store.token(), None);
    }
}
