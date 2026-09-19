//! Install = download the release zip, unpack its `gdaccess/` folder into a staging folder next to the
//! install folder, swap it in (the old install is replaced wholesale so nothing stale lingers), put a copy of
//! this installer inside it (Add/Remove Programs runs that copy), then the shortcuts and the registry entry.
//! The mod's own data folder (log, settings) is never touched.
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Component, Path, PathBuf};

use super::paths::{install_dir, INSTALLER_EXE, LAUNCHER_EXE, MOD_DLL, VERSION_FILE, ZIP_ROOT};
use super::shortcuts;

/// Progress as the worker sees it; the GUI shows it, the CLI prints it.
#[derive(Debug, Clone)]
pub enum Progress {
    Status(String),
    Percent(u32),
}

pub fn download_bytes(url: &str, progress: &dyn Fn(Progress)) -> Result<Vec<u8>, String> {
    let client = reqwest::blocking::Client::builder()
        .user_agent("GDAccessInstaller")
        .timeout(std::time::Duration::from_secs(300))
        .build()
        .map_err(|e| format!("Failed to create the HTTP client: {}", e))?;
    let mut resp = client.get(url).send().map_err(|e| format!("Download failed: {}", e))?;
    if !resp.status().is_success() {
        return Err(format!("Download returned status {}", resp.status()));
    }
    let total = resp.content_length().unwrap_or(0);
    let mut out = Vec::with_capacity(total as usize);
    let mut buf = [0u8; 64 * 1024];
    let mut last_pct = u32::MAX;
    loop {
        let n = resp.read(&mut buf).map_err(|e| format!("Download read error: {}", e))?;
        if n == 0 {
            break;
        }
        out.extend_from_slice(&buf[..n]);
        if total > 0 {
            let pct = (out.len() as u64 * 100 / total) as u32;
            if pct != last_pct {
                last_pct = pct;
                progress(Progress::Percent(pct));
            }
        }
    }
    Ok(out)
}

pub fn download_and_install(url: &str, progress: &dyn Fn(Progress)) -> Result<(), String> {
    progress(Progress::Status("Downloading...".into()));
    let data = download_bytes(url, progress)?;
    install_zip(&data, progress)
}

pub fn install_from_file(zip_path: &Path, progress: &dyn Fn(Progress)) -> Result<(), String> {
    let data = fs::read(zip_path).map_err(|e| format!("Failed to read {}: {}", zip_path.display(), e))?;
    install_zip(&data, progress)
}

/// A zip entry name as a safe relative path (no absolute parts, no `..`).
fn sanitize(name: &str) -> Result<PathBuf, String> {
    let p = Path::new(name);
    let mut out = PathBuf::new();
    for c in p.components() {
        match c {
            Component::Normal(s) => out.push(s),
            Component::CurDir => {}
            _ => return Err(format!("Refusing zip entry with an unsafe path: {}", name)),
        }
    }
    Ok(out)
}

pub fn install_zip(data: &[u8], progress: &dyn Fn(Progress)) -> Result<(), String> {
    let dest = install_dir();
    let staging = dest.with_file_name("GD Access.installing");
    let old = dest.with_file_name("GD Access.old");
    progress(Progress::Status("Unpacking...".into()));

    let mut archive = zip::ZipArchive::new(Cursor::new(data)).map_err(|e| format!("Not a valid zip: {}", e))?;
    let _ = fs::remove_dir_all(&staging);
    fs::create_dir_all(&staging).map_err(|e| format!("Failed to create {}: {}", staging.display(), e))?;
    let mut wrote = 0usize;
    for i in 0..archive.len() {
        let mut file = archive.by_index(i).map_err(|e| format!("Failed to read a zip entry: {}", e))?;
        let name = file.name().to_string();
        let rel = sanitize(&name)?;
        // Only the payload folder; anything else in the zip is ignored.
        let Ok(inner) = rel.strip_prefix(ZIP_ROOT) else { continue };
        if inner.as_os_str().is_empty() {
            continue;
        }
        let target = staging.join(inner);
        if name.ends_with('/') {
            fs::create_dir_all(&target).map_err(|e| format!("Failed to create {}: {}", target.display(), e))?;
            continue;
        }
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent).map_err(|e| format!("Failed to create {}: {}", parent.display(), e))?;
        }
        let mut contents = Vec::new();
        file.read_to_end(&mut contents).map_err(|e| format!("Failed to read {} from the zip: {}", name, e))?;
        fs::write(&target, &contents).map_err(|e| format!("Failed to write {}: {}", target.display(), e))?;
        wrote += 1;
    }
    for required in [LAUNCHER_EXE, MOD_DLL, "prism.dll"] {
        if !staging.join(required).exists() {
            let _ = fs::remove_dir_all(&staging);
            return Err(format!("The zip does not contain {}/{} -- is this a GD Access release zip?", ZIP_ROOT, required));
        }
    }
    if !staging.join(VERSION_FILE).exists() {
        fs::write(staging.join(VERSION_FILE), "unknown\n").map_err(|e| format!("Failed to write version.txt: {}", e))?;
    }
    // Add/Remove Programs runs the installer from inside the install folder.
    if let Ok(me) = std::env::current_exe() {
        let _ = fs::copy(&me, staging.join(INSTALLER_EXE));
    }

    progress(Progress::Status("Installing...".into()));
    let _ = fs::remove_dir_all(&old);
    if dest.exists() {
        fs::rename(&dest, &old).map_err(|e| {
            format!("Could not replace the existing install at {} -- close GD Access and the game if they are running, then try again. ({})", dest.display(), e)
        })?;
    }
    if let Some(parent) = dest.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Err(e) = fs::rename(&staging, &dest) {
        let _ = fs::rename(&old, &dest); // put the previous install back
        return Err(format!("Failed to move the new files into {}: {}", dest.display(), e));
    }
    let _ = fs::remove_dir_all(&old);

    progress(Progress::Status("Creating shortcuts...".into()));
    shortcuts::create_all(&dest)?;
    progress(Progress::Status(format!("Installed {} files.", wrote)));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitize_rejects_traversal() {
        assert!(sanitize("gdaccess/../x").is_err());
        assert!(sanitize("C:/x").is_err());
        assert_eq!(sanitize("gdaccess/assets/a.wav").unwrap(), PathBuf::from("gdaccess/assets/a.wav"));
    }
}
