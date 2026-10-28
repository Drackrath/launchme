use std::io::Write;
use std::sync::Mutex;

use serde_json::{Value, json};

#[derive(Debug)]
pub struct Failure {
    pub code: &'static str,
    pub message: String,
}

impl Failure {
    pub fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }

    // Transport failures are "network"; anything else from a service keeps its text.
    pub fn network(error: impl std::fmt::Display) -> Self {
        Self::new("network", error.to_string())
    }
}

pub type Outcome = Result<Value, Failure>;

static PANIC: Mutex<String> = Mutex::new(String::new());

// Xodus still panics on unexpected service answers; the message becomes the "internal" failure.
pub fn install_panic_hook() {
    std::panic::set_hook(Box::new(|info| {
        let text = info
            .payload()
            .downcast_ref::<&str>()
            .map(|s| s.to_string())
            .or_else(|| info.payload().downcast_ref::<String>().cloned())
            .unwrap_or_else(|| "panic".to_string());
        let location = info
            .location()
            .map(|l| format!(" ({}:{})", l.file(), l.line()))
            .unwrap_or_default();
        if let Ok(mut slot) = PANIC.lock() {
            *slot = format!("{text}{location}");
        }
        log(&format!("panic: {text}{location}"));
    }));
}

pub fn last_panic() -> String {
    PANIC
        .lock()
        .map(|s| s.clone())
        .unwrap_or_else(|_| "panic".to_string())
}

pub fn log(line: &str) {
    let mut err = std::io::stderr().lock();
    let _ = writeln!(err, "{}", line.replace(['\r', '\n'], " "));
}

// One progress line; Launchme reads the newest per stage.
pub fn progress(stage: &str, done: u64, total: u64) {
    let line = json!({ "progress": { "stage": stage, "done": done, "total": total } });
    let mut err = std::io::stderr().lock();
    let _ = writeln!(err, "{line}");
    let _ = err.flush();
}

// A message box for the one command started without Launchme reading its output (`run`).
#[cfg(windows)]
pub fn alert(message: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MB_ICONERROR, MB_OK, MB_SETFOREGROUND, MB_TOPMOST, MessageBoxW};
    let wide = |s: &str| s.encode_utf16().chain(std::iter::once(0)).collect::<Vec<u16>>();
    let text = wide(&format!("The game could not start.\n\n{message}"));
    let title = wide("Launchme · Xbox");
    // SAFETY: both strings are NUL-terminated UTF-16 buffers that outlive the call.
    unsafe { MessageBoxW(std::ptr::null_mut(), text.as_ptr(), title.as_ptr(), MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND) };
}

// Linux: the log line, and a desktop notification when a notification daemon answers.
#[cfg(not(windows))]
pub fn alert(message: &str) {
    log(message);
    let _ = std::process::Command::new("notify-send")
        .args(["--app-name=Launchme", "--icon=dialog-error", "Launchme · Xbox"])
        .arg(format!("The game could not start. {message}"))
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
}

pub fn finish(outcome: Outcome) -> i32 {
    let (value, code) = match outcome {
        Ok(mut value) => {
            if let Value::Object(map) = &mut value {
                map.insert("ok".into(), Value::Bool(true));
            }
            (value, 0)
        }
        Err(failure) => (
            json!({ "ok": false, "error": failure.code, "message": failure.message }),
            match failure.code {
                "usage" => 2,
                "cancelled" => 3,
                _ => 1,
            },
        ),
    };
    let mut out = std::io::stdout().lock();
    let _ = writeln!(out, "{value}");
    let _ = out.flush();
    code
}
