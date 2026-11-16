//! Browser sign-in with OpenID Connect: a silent refresh with the refresh
//! token kept in the OS credential store (Windows Credential Manager), or
//! the authorization-code flow with PKCE in the system browser, which
//! returns to a one-shot listener on a free 127.0.0.1 port (RFC 8252: the
//! provider registers `http://127.0.0.1/callback` and ignores the port). Each sign-in runs
//! on its own thread and reports through a channel, so the game never waits.
//! The thread never touches the credential store: it is handed the stored
//! refresh token and returns what to change, which the owner (client.rs)
//! applies only when it accepts that sign-in's result.
//!
//! The result is an ID token, which the server accepts as the connection's
//! token. Another sign-in method (Steam, docs/todo/launch_todo.md) produces the same
//! `LoginMessage::Done`.

use oauth2::basic::{
    BasicErrorResponse, BasicErrorResponseType, BasicRevocationErrorResponse, BasicTokenIntrospectionResponse, BasicTokenType,
};
use oauth2::{
    AuthUrl, AuthorizationCode, Client, ClientId, CsrfToken, EndpointNotSet, EndpointSet, ExtraTokenFields,
    PkceCodeChallenge, RedirectUrl, RefreshToken, RequestTokenError, Scope, StandardRevocableToken, StandardTokenResponse,
    TokenResponse, TokenUrl,
};
use serde::{Deserialize, Serialize};
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

/// How long the browser sign-in waits for the player.
const BROWSER_TIMEOUT: Duration = Duration::from_secs(300);
/// How long the listener waits for a connected browser to send its request.
const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
/// The credential store service that keeps the refresh token.
const KEYRING_SERVICE: &str = "golfpp";

// Why a sign-in failed: the id part of a LoginFailed reason ("id: detail"),
// which the game turns into text (stdb_bridge.h lists them).
pub const FAILED_CANCELLED: &str = "sign_in_cancelled";
pub const FAILED_TIMED_OUT: &str = "sign_in_timed_out";
pub const FAILED_BROWSER: &str = "browser_failed";
pub const FAILED_SIGN_IN: &str = "sign_in_failed";
pub const FAILED_NO_STORED_SIGN_IN: &str = "no_stored_sign_in";

pub fn failure(id: &str, detail: impl std::fmt::Display) -> String {
    format!("{id}: {detail}")
}

#[derive(Clone)]
pub struct AuthConfig {
    pub issuer: String,
    pub client_id: String,
    pub scopes: String,
    pub authorization_endpoint: String,
    pub token_endpoint: String,
    /// What the browser shows afterwards (the game's string table).
    pub page_signed_in: String,
    pub page_failed: String,
}

/// What a finished sign-in asks of the credential store.
#[derive(Debug, PartialEq)]
pub enum CredentialChange {
    Keep,
    Store(String),
    /// The server refused this refresh token (invalid_grant): forget it,
    /// unless another has replaced it since.
    Forget(String),
}

pub enum LoginMessage {
    WaitingForBrowser,
    /// The ID token or why there is none, and the credential change.
    Done(Result<String, String>, CredentialChange),
}

pub struct LoginJob {
    pub messages: Receiver<LoginMessage>,
    cancel: Arc<AtomicBool>,
    /// Tests join the sign-in's thread to know it has finished.
    #[cfg(test)]
    pub worker: Option<thread::JoinHandle<()>>,
}

impl Drop for LoginJob {
    fn drop(&mut self) {
        self.cancel.store(true, Ordering::SeqCst);
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct IdTokenFields {
    #[serde(default)]
    id_token: Option<String>,
}
impl ExtraTokenFields for IdTokenFields {}

type IdTokenResponse = StandardTokenResponse<IdTokenFields, BasicTokenType>;
type OidcClient = Client<
    BasicErrorResponse,
    IdTokenResponse,
    BasicTokenIntrospectionResponse,
    StandardRevocableToken,
    BasicRevocationErrorResponse,
    EndpointSet,
    EndpointNotSet,
    EndpointNotSet,
    EndpointNotSet,
    EndpointSet,
>;

/// Where the refresh token is kept; used only by the owner, on the game's thread.
pub trait CredentialStore {
    /// The stored refresh token, or why there is none.
    fn load(&self) -> Result<String, String>;
    fn save(&self, refresh_token: &str);
    /// Forgets it, so the next sign-in uses the browser.
    fn forget(&self);
}

/// The OS credential store.
pub struct OsCredentialStore(Option<keyring::Entry>);

impl OsCredentialStore {
    pub fn new(config: &AuthConfig) -> OsCredentialStore {
        OsCredentialStore(keyring_entry(config))
    }
}

impl CredentialStore for OsCredentialStore {
    fn load(&self) -> Result<String, String> {
        let entry = self.0.as_ref().ok_or("no credential store")?;
        entry.get_password().map_err(|e| e.to_string())
    }

    fn save(&self, refresh_token: &str) {
        if let Some(entry) = &self.0 {
            let _ = entry.set_password(refresh_token);
        }
    }

    fn forget(&self) {
        if let Some(entry) = &self.0 {
            let _ = entry.delete_credential();
        }
    }
}

pub fn apply(store: &dyn CredentialStore, change: CredentialChange) {
    match change {
        CredentialChange::Keep => {}
        CredentialChange::Store(refresh_token) => store.save(&refresh_token),
        CredentialChange::Forget(refused) => {
            if store.load().is_ok_and(|stored| stored == refused) {
                store.forget();
            }
        }
    }
}

fn keyring_entry(config: &AuthConfig) -> Option<keyring::Entry> {
    keyring::Entry::new(KEYRING_SERVICE, &format!("refresh_token:{}:{}", config.issuer, config.client_id)).ok()
}

#[cfg(all(test, target_os = "windows"))]
mod credential_tests {
    use super::*;
    use crate::client::StdbClient;

    #[test]
    fn guest_sign_out_preserves_browser_sign_in() {
        let unique = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let config = AuthConfig {
            issuer: "https://cleanup2.invalid".into(), client_id: format!("test-{}-{unique}", std::process::id()),
            scopes: String::new(), authorization_endpoint: String::new(), token_endpoint: String::new(),
            page_signed_in: String::new(), page_failed: String::new(),
        };
        struct TestCredential(keyring::Entry);
        impl Drop for TestCredential {
            fn drop(&mut self) { let _ = self.0.delete_credential(); }
        }
        let entry = keyring_entry(&config).unwrap();
        assert!(matches!(entry.get_password(), Err(keyring::Error::NoEntry)));
        entry.set_password("disposable-test-credential").unwrap();
        let entry = TestCredential(entry);
        let store = |config: &AuthConfig| Box::new(OsCredentialStore::new(config));
        StdbClient::new(String::new(), String::new(), config.clone(), true, store(&config)).sign_out();
        assert!(entry.0.get_password().is_ok());
        StdbClient::new(String::new(), String::new(), config.clone(), false, store(&config)).sign_out();
        assert!(matches!(entry.0.get_password(), Err(keyring::Error::NoEntry)));
    }
}

fn client(config: &AuthConfig, redirect: Option<&str>) -> Result<OidcClient, String> {
    let auth = AuthUrl::new(config.authorization_endpoint.clone()).map_err(|e| failure(FAILED_SIGN_IN, e))?;
    let token = TokenUrl::new(config.token_endpoint.clone()).map_err(|e| failure(FAILED_SIGN_IN, e))?;
    let mut client: OidcClient = Client::new(ClientId::new(config.client_id.clone())).set_auth_uri(auth).set_token_uri(token);
    if let Some(redirect) = redirect {
        client = client.set_redirect_uri(RedirectUrl::new(redirect.to_string()).map_err(|e| failure(FAILED_SIGN_IN, e))?);
    }
    Ok(client)
}

fn http_client() -> Result<oauth2::reqwest::blocking::Client, String> {
    oauth2::reqwest::blocking::ClientBuilder::new()
        // No redirects, so a token request cannot be bounced elsewhere (SSRF).
        .redirect(oauth2::reqwest::redirect::Policy::none())
        .build()
        .map_err(|e| failure(FAILED_SIGN_IN, e))
}

/// Refreshes with the stored refresh token; a failure says whether the
/// server refused that token.
fn refresh(config: &AuthConfig, stored: &str) -> Result<IdTokenResponse, (String, bool)> {
    let client = client(config, None).map_err(|e| (e, false))?;
    client
        .exchange_refresh_token(&RefreshToken::new(stored.to_string()))
        .request(&http_client().map_err(|e| (e, false))?)
        .map_err(|e| {
            // Only an expired or revoked refresh token is forgotten: no
            // network or a server error must not sign the player out.
            let refused = matches!(&e, RequestTokenError::ServerResponse(response)
                if *response.error() == BasicErrorResponseType::InvalidGrant);
            (failure(FAILED_SIGN_IN, format!("refresh: {e}")), refused)
        })
}

/// `%xx` and `+` decoding of one query value.
fn percent_decode(value: &str) -> String {
    let bytes = value.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < bytes.len() => {
                let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok());
                match hex {
                    Some(b) => {
                        out.push(b);
                        i += 2;
                    }
                    None => out.push(b'%'),
                }
            }
            b => out.push(b),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// The query parameter `name` of an HTTP request line ("GET /callback?a=b HTTP/1.1").
fn query_value(request_line: &str, name: &str) -> Option<String> {
    let target = request_line.split_whitespace().nth(1)?;
    let query = target.split_once('?')?.1;
    query.split('&').find_map(|pair| {
        let (key, value) = pair.split_once('=').unwrap_or((pair, ""));
        (key == name).then(|| percent_decode(value))
    })
}

/// `text` as HTML text.
fn html_escape(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

fn respond(mut stream: TcpStream, message: &str) {
    let body = format!("<html><head><meta charset=\"utf-8\"></head><body><p>{}</p></body></html>", html_escape(message));
    let reply = format!(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        body.len(),
        body
    );
    let _ = stream.write_all(reply.as_bytes());
}

/// Reads the request line of one HTTP request.
fn read_request_line(stream: &mut TcpStream) -> Option<String> {
    stream.set_nonblocking(false).ok()?;
    stream.set_read_timeout(Some(REQUEST_TIMEOUT)).ok()?;
    let mut buffer = Vec::new();
    let mut chunk = [0u8; 1024];
    while !buffer.windows(2).any(|w| w == b"\r\n") && buffer.len() < 16 * 1024 {
        let read = stream.read(&mut chunk).ok()?;
        if read == 0 {
            break;
        }
        buffer.extend_from_slice(&chunk[..read]);
    }
    let text = String::from_utf8_lossy(&buffer);
    text.lines().next().map(str::to_string)
}

/// The path the browser returns to; the port is whichever was free.
const CALLBACK_PATH: &str = "/callback";

fn browser_sign_in(config: &AuthConfig, cancel: &AtomicBool, messages: &Sender<LoginMessage>) -> Result<IdTokenResponse, String> {
    let listener = TcpListener::bind("127.0.0.1:0").map_err(|e| failure(FAILED_BROWSER, e))?;
    let port = listener.local_addr().map_err(|e| failure(FAILED_BROWSER, e))?.port();
    listener.set_nonblocking(true).map_err(|e| failure(FAILED_BROWSER, e))?;
    let redirect = format!("http://127.0.0.1:{port}{CALLBACK_PATH}");
    let client = client(config, Some(&redirect))?;

    let (challenge, verifier) = PkceCodeChallenge::new_random_sha256();
    // OIDC providers grant offline_access (the refresh token that signs in
    // silently next time) only when consent was asked for (OIDC Core 11).
    let mut request = client
        .authorize_url(CsrfToken::new_random)
        .set_pkce_challenge(challenge)
        .add_extra_param("prompt", "consent");
    for scope in config.scopes.split_whitespace() {
        request = request.add_scope(Scope::new(scope.to_string()));
    }
    let (url, state) = request.url();
    open::that(url.as_str()).map_err(|e| failure(FAILED_BROWSER, e))?;
    let _ = messages.send(LoginMessage::WaitingForBrowser);

    let started = Instant::now();
    loop {
        if cancel.load(Ordering::SeqCst) {
            return Err(failure(FAILED_CANCELLED, "by the player"));
        }
        if started.elapsed() > BROWSER_TIMEOUT {
            return Err(failure(FAILED_TIMED_OUT, "no answer from the browser"));
        }
        match listener.accept() {
            Ok((mut stream, _)) => {
                let Some(line) = read_request_line(&mut stream) else { continue };
                if line.split_whitespace().nth(1).and_then(|target| target.split('?').next()) != Some(CALLBACK_PATH) {
                    continue; // e.g. a favicon request
                }
                // A request without our state is not our browser's answer
                // (another page probing the port, say): ignore it.
                if query_value(&line, "state").as_deref() != Some(state.secret().as_str()) {
                    respond(stream, &config.page_failed);
                    continue;
                }
                let Some(code) = query_value(&line, "code") else {
                    respond(stream, &config.page_failed);
                    let reason = query_value(&line, "error").unwrap_or_else(|| "no code".to_string());
                    return Err(failure(FAILED_SIGN_IN, reason));
                };
                let response = client
                    .exchange_code(AuthorizationCode::new(code))
                    .set_pkce_verifier(verifier)
                    .request(&http_client()?)
                    .map_err(|e| failure(FAILED_SIGN_IN, format!("code exchange: {e}")));
                respond(stream, if response.is_ok() { &config.page_signed_in } else { &config.page_failed });
                return response;
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(100)),
            Err(e) => return Err(failure(FAILED_BROWSER, e)),
        }
    }
}

/// Signs in silently with `stored` (the refresh token the owner read, or
/// why there is none), else (unless `silent_only`) through the browser.
fn sign_in(
    config: &AuthConfig,
    stored: Result<String, String>,
    silent_only: bool,
    cancel: &AtomicBool,
    messages: &Sender<LoginMessage>,
) -> (Result<String, String>, CredentialChange) {
    let mut change = CredentialChange::Keep;
    let refreshed = match stored {
        Err(why) => Err(failure(FAILED_NO_STORED_SIGN_IN, why)),
        Ok(token) => refresh(config, &token).map_err(|(reason, refused)| {
            if refused {
                change = CredentialChange::Forget(token.clone());
            }
            reason
        }),
    };
    let response = refreshed.or_else(|refresh_error| {
        if silent_only {
            Err(refresh_error)
        } else if cancel.load(Ordering::SeqCst) {
            Err(failure(FAILED_CANCELLED, "by the player"))
        } else {
            browser_sign_in(config, cancel, messages)
        }
    });
    match response {
        Ok(response) => {
            if let Some(refresh_token) = response.refresh_token() {
                change = CredentialChange::Store(refresh_token.secret().clone());
            }
            let id_token = response.extra_fields().id_token.clone();
            (id_token.ok_or_else(|| failure(FAILED_SIGN_IN, "no id_token in the token response")), change)
        }
        Err(reason) => (Err(reason), change),
    }
}

/// Signs in on a new thread (see sign_in). Dropping the job cancels it and
/// discards its result.
pub fn start(config: AuthConfig, stored: Result<String, String>, silent_only: bool) -> LoginJob {
    let (sender, messages) = channel();
    let cancel = Arc::new(AtomicBool::new(false));
    let job_cancel = cancel.clone();
    let worker = thread::spawn(move || {
        let (token, credential) = sign_in(&config, stored, silent_only, &job_cancel, &sender);
        let _ = sender.send(LoginMessage::Done(token, credential));
    });
    #[cfg(not(test))]
    drop(worker);
    LoginJob {
        messages,
        cancel,
        #[cfg(test)]
        worker: Some(worker),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn query_values_are_found_and_decoded() {
        let line = "GET /callback?code=a%2Fb+c&state=xyz&empty= HTTP/1.1";
        assert_eq!(query_value(line, "code").as_deref(), Some("a/b c"));
        assert_eq!(query_value(line, "state").as_deref(), Some("xyz"));
        assert_eq!(query_value(line, "empty").as_deref(), Some(""));
        assert_eq!(query_value(line, "missing"), None);
        assert_eq!(query_value("GET /callback HTTP/1.1", "code"), None);
        assert_eq!(percent_decode("100%"), "100%");
        assert_eq!(percent_decode("%zz"), "%zz");
    }

    #[test]
    fn page_text_is_escaped() {
        assert_eq!(html_escape("<b>&\"</b>"), "&lt;b&gt;&amp;&quot;&lt;/b&gt;");
    }
}
