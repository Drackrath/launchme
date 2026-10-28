// Persistent token tier for Xodus' TokenManager: one file per key in <profile>\tokens, each
// encrypted with DPAPI for the current Windows user (Xodus' default on Windows would be a plain
// keyring file). Short-lived XSTS tokens stay in Xodus' in-memory tier.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use xodus::tokens::TokenManager;
use xodus::tokens::backend::MemoryBackend;
use xodus::tokens::store::{TokenBackend, TokenStoreError};

pub struct DpapiBackend {
    folder: PathBuf,
}

impl DpapiBackend {
    pub fn new(profile: &Path) -> Self {
        Self {
            folder: profile.join("tokens"),
        }
    }

    fn path(&self, key: &str) -> PathBuf {
        // Keys are Xodus' own constants ("dev_license", "user-tokens"...); anything else is hex.
        let safe = key
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'_');
        let name = if safe {
            key.to_string()
        } else {
            key.bytes().map(|b| format!("{b:02x}")).collect()
        };
        self.folder.join(format!("{name}.dpapi"))
    }
}

impl TokenBackend for DpapiBackend {
    fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
        match std::fs::read(self.path(key)) {
            Ok(sealed) => Ok(Some(dpapi::unprotect(&sealed)?)),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
        std::fs::create_dir_all(&self.folder)?;
        let sealed = dpapi::protect(value)?;
        let path = self.path(key);
        let part = path.with_extension("part");
        std::fs::write(&part, sealed)?;
        std::fs::rename(&part, &path)?;
        Ok(())
    }

    fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
        match std::fs::remove_file(self.path(key)) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(()),
            Err(e) => Err(e.into()),
        }
    }
}

static PROFILE: std::sync::OnceLock<PathBuf> = std::sync::OnceLock::new();

// Drops the device's STS token (kept: its registration and keys), so the next
// ensure_device_credentials signs the device in again. Xodus never renews an expired one itself.
pub fn forget_device_token() {
    if let Some(profile) = PROFILE.get() {
        let _ = DpapiBackend::new(profile).remove("device-tokens");
    }
}

pub fn token_manager(profile: &Path) -> TokenManager {
    let _ = PROFILE.set(profile.to_path_buf());
    TokenManager::new(
        Arc::new(DpapiBackend::new(profile)),
        Arc::new(MemoryBackend::default()),
    )
}

#[cfg(windows)]
mod dpapi {
    use windows_sys::Win32::Foundation::{HLOCAL, LocalFree};
    use windows_sys::Win32::Security::Cryptography::{
        CRYPT_INTEGER_BLOB, CRYPTPROTECT_UI_FORBIDDEN, CryptProtectData, CryptUnprotectData,
    };

    fn call(
        input: &[u8],
        f: impl FnOnce(*const CRYPT_INTEGER_BLOB, *mut CRYPT_INTEGER_BLOB) -> i32,
    ) -> std::io::Result<Vec<u8>> {
        let blob = CRYPT_INTEGER_BLOB {
            cbData: u32::try_from(input.len()).map_err(std::io::Error::other)?,
            pbData: input.as_ptr() as *mut u8,
        };
        let mut out = CRYPT_INTEGER_BLOB {
            cbData: 0,
            pbData: std::ptr::null_mut(),
        };
        if f(&blob, &mut out) == 0 {
            return Err(std::io::Error::last_os_error());
        }
        // SAFETY: on success DPAPI returns a LocalAlloc'd buffer of cbData bytes.
        let data = unsafe { std::slice::from_raw_parts(out.pbData, out.cbData as usize).to_vec() };
        unsafe { LocalFree(out.pbData as HLOCAL) };
        Ok(data)
    }

    pub fn protect(data: &[u8]) -> std::io::Result<Vec<u8>> {
        call(data, |input, output| unsafe {
            CryptProtectData(
                input,
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                CRYPTPROTECT_UI_FORBIDDEN,
                output,
            )
        })
    }

    pub fn unprotect(data: &[u8]) -> std::io::Result<Vec<u8>> {
        call(data, |input, output| unsafe {
            CryptUnprotectData(
                input,
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                CRYPTPROTECT_UI_FORBIDDEN,
                output,
            )
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn values_round_trip_sealed() {
        let profile = std::env::temp_dir().join(format!("launchme-xbox-store-{}", std::process::id()));
        let backend = DpapiBackend::new(&profile);
        backend.set("user-tokens", b"secret value").unwrap();
        let raw = std::fs::read(profile.join("tokens").join("user-tokens.dpapi")).unwrap();
        #[cfg(windows)]
        assert!(!raw.windows(6).any(|w| w == b"secret"));
        assert_eq!(backend.get("user-tokens").unwrap().as_deref(), Some(&b"secret value"[..]));
        assert_eq!(backend.get("http://x/y").unwrap(), None);
        backend.remove("user-tokens").unwrap();
        backend.remove("user-tokens").unwrap();
        assert_eq!(backend.get("user-tokens").unwrap(), None);
        let _ = std::fs::remove_dir_all(profile);
    }
}

// Linux/macOS builds (the later Xodus targets) keep plain files inside the private profile.
#[cfg(not(windows))]
mod dpapi {
    pub fn protect(data: &[u8]) -> std::io::Result<Vec<u8>> {
        Ok(data.to_vec())
    }
    pub fn unprotect(data: &[u8]) -> std::io::Result<Vec<u8>> {
        Ok(data.to_vec())
    }
}
