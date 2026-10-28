// A session of a game installed by `install`. Xodus on Linux decrypts the package's encrypted
// files into memory for Wine (WINE_DLL_FILE_MAP); Windows has no such loader hook, so
// `launch-prepare` asks for a fresh license and decrypts them next to the encrypted copies
// (<file>.xodus-enc) for the session, and `launch-cleanup` puts the encrypted ones back.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use msixvc::xvd::{SegmentFile, XvdFile};
use serde_json::{Value, json};
use tokio::fs::{File, OpenOptions};
use xodus::tokens::TokenManager;

use crate::install::{HEADER, MARKER};
use crate::output::{Failure, Outcome};

const SESSION: &str = ".launchme-xbox-session.json";
const ENCRYPTED: &str = "xodus-enc";

fn storage(path: &Path, e: impl std::fmt::Display) -> Failure {
    Failure::new("storage", format!("{}: {e}", path.display()))
}

fn encrypted_copy(path: &Path) -> PathBuf {
    let mut name = path.as_os_str().to_owned();
    name.push(".");
    name.push(ENCRYPTED);
    PathBuf::from(name)
}

fn relative(name: &str) -> Option<PathBuf> {
    let path = PathBuf::from(name.replace('/', "\\").trim_start_matches('\\'));
    path.components()
        .all(|c| matches!(c, std::path::Component::Normal(_)))
        .then_some(path)
}

async fn encrypted_files(header: &Path) -> Result<(XvdFile, HashMap<String, SegmentFile>), Failure> {
    let internal = |e: Box<dyn std::error::Error>| Failure::new("internal", e.to_string());
    let mut file = OpenOptions::new()
        .read(true)
        .open(header)
        .await
        .map_err(|_| Failure::new("not_found", "not installed by Launchme (no package header)"))?;
    let xvd = XvdFile::parse(&mut file).await.map_err(internal)?;
    let mut files = HashMap::new();
    for (name, entry) in &xvd.parse_user_package_files(&mut file).await.map_err(internal)? {
        if name == "SegmentMetadata.bin" {
            files = xvd.parse_segment_metadata(&mut file, entry).await.map_err(internal)?;
        }
    }
    if files.is_empty() {
        files = xvd.parse_ntfs_segment_metadata(&mut file, false).await.map_err(internal)?;
    }
    files.retain(|_, f| f.keep_encrypted);
    Ok((xvd, files))
}

fn executable(dir: &Path) -> Option<String> {
    let marker: Value = serde_json::from_slice(&std::fs::read(dir.join(MARKER)).ok()?).ok()?;
    marker["executable"]
        .as_str()
        .map(str::to_owned)
        .or_else(|| {
            crate::config::read(dir)
                .and_then(|c| c.executables.first().cloned())
                .map(|p| p.to_string_lossy().into_owned())
        })
}

pub async fn prepare(client: &reqwest::Client, tokens: &TokenManager, dir: &Path, market: &str) -> Outcome {
    if dir.join(SESSION).exists() {
        cleanup(dir).await?;
    }
    let (xvd, files) = encrypted_files(&dir.join(HEADER)).await?;
    let key = crate::license::content_key(client, tokens, &xvd.content_id().to_string(), market).await?;
    let mut decrypted: Vec<String> = Vec::new();
    let total: u64 = files.values().map(|f| f.length).sum();
    let mut done = 0u64;
    let session = dir.join(SESSION);
    for (name, entry) in &files {
        let Some(rel) = relative(name) else { continue };
        let plain = dir.join(&rel);
        let sealed = encrypted_copy(&plain);
        if !sealed.exists() {
            if !plain.exists() {
                continue; // Not part of this install (skipped NTFS metadata, optional file).
            }
            std::fs::rename(&plain, &sealed).map_err(|e| storage(&plain, e))?;
        }
        // Recorded before writing so a crash still lets cleanup restore it.
        decrypted.push(rel.to_string_lossy().into_owned());
        std::fs::write(&session, serde_json::to_vec(&json!({ "files": decrypted })).expect("json"))
            .map_err(|e| storage(&session, e))?;
        let mut input = File::open(&sealed).await.map_err(|e| storage(&sealed, e))?;
        let mut output = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&plain)
            .await
            .map_err(|e| storage(&plain, e))?;
        xvd.mount_mem_fd(&mut input, &mut output, entry, key, |_, _| {})
            .await
            .map_err(|e| Failure::new("internal", format!("{name}: {e}")))?;
        done += entry.length;
        crate::output::progress("decrypt", done, total);
    }
    Ok(json!({ "executable": executable(dir), "decrypted": decrypted }))
}

// Launchme's Graj for a game it installed: prepare, start the game from the package root, wait for
// it (Launchme's job holds both processes, so its session and "Zamknij grę" see the game), then put
// the encrypted files back. A failure is shown to the user: nothing else would tell them.
pub async fn run(client: &reqwest::Client, tokens: &TokenManager, dir: &Path, market: &str) -> Outcome {
    let result: Outcome = async {
        let prepared = prepare(client, tokens, dir, market).await?;
        let relative = prepared["executable"]
            .as_str()
            .and_then(relative)
            .ok_or_else(|| Failure::new("not_found", "MicrosoftGame.config names no PC executable"))?;
        let exe = dir.join(relative);
        let status = tokio::process::Command::new(&exe)
            .current_dir(dir)
            .status()
            .await
            .map_err(|e| Failure::new("internal", format!("{}: {e}", exe.display())));
        let cleaned = cleanup(dir).await;
        let status = status?;
        cleaned?;
        Ok(json!({ "exit_code": status.code() }))
    }
    .await;
    if let Err(failure) = &result {
        crate::output::alert(&failure.message);
    }
    result
}

pub async fn cleanup(dir: &Path) -> Outcome {
    let session = dir.join(SESSION);
    let files: Vec<String> = match std::fs::read(&session) {
        Ok(bytes) => serde_json::from_slice::<Value>(&bytes)
            .ok()
            .and_then(|v| v["files"].as_array().cloned())
            .unwrap_or_default()
            .into_iter()
            .filter_map(|v| v.as_str().map(str::to_owned))
            .collect(),
        Err(_) => return Ok(json!({ "restored": 0 })),
    };
    let mut restored = 0;
    for name in &files {
        let Some(rel) = relative(name) else { continue };
        let plain = dir.join(rel);
        let sealed = encrypted_copy(&plain);
        if !sealed.exists() {
            continue;
        }
        if plain.exists() {
            std::fs::remove_file(&plain).map_err(|e| storage(&plain, e))?;
        }
        std::fs::rename(&sealed, &plain).map_err(|e| storage(&sealed, e))?;
        restored += 1;
    }
    std::fs::remove_file(&session).map_err(|e| storage(&session, e))?;
    Ok(json!({ "restored": restored }))
}
