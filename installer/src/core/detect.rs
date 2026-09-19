//! What is on this machine: the game (through Steam's own records, the same walk gdlaunch does at run time --
//! only a status line here, the launcher finds the game itself) and the installed mod's version.
use std::path::{Path, PathBuf};

use super::paths::{install_dir, launcher_path, VERSION_FILE};

const APP_ID: &str = "219990";

/// Every `"key"  "value"` occurrence in a Valve KeyValues text file, with `\\` unescaped.
pub fn vdf_values(text: &str, key: &str) -> Vec<String> {
    let needle = format!("\"{}\"", key);
    let mut out = Vec::new();
    let mut rest = text;
    while let Some(i) = rest.find(&needle) {
        rest = &rest[i + needle.len()..];
        let Some(q1) = rest.find('"') else { break };
        let Some(q2) = rest[q1 + 1..].find('"') else { break };
        out.push(rest[q1 + 1..q1 + 1 + q2].replace("\\\\", "\\"));
        rest = &rest[q1 + 1 + q2 + 1..];
    }
    out
}

fn steam_path() -> Option<PathBuf> {
    use winreg::enums::HKEY_CURRENT_USER;
    use winreg::RegKey;
    let key = RegKey::predef(HKEY_CURRENT_USER).open_subkey("Software\\Valve\\Steam").ok()?;
    let s: String = key.get_value("SteamPath").ok()?;
    Some(PathBuf::from(s.replace('/', "\\")))
}

/// The 64-bit game exe, from the Steam library whose steamapps\ holds the app's manifest.
pub fn detect_game_exe() -> Option<PathBuf> {
    let steam = steam_path()?;
    let mut libs = vec![steam.clone()];
    if let Ok(text) = std::fs::read_to_string(steam.join("steamapps").join("libraryfolders.vdf")) {
        libs.extend(vdf_values(&text, "path").into_iter().map(PathBuf::from));
    }
    for lib in libs {
        let manifest = lib.join("steamapps").join(format!("appmanifest_{}.acf", APP_ID));
        let Ok(text) = std::fs::read_to_string(&manifest) else { continue };
        let Some(dir) = vdf_values(&text, "installdir").into_iter().next() else { continue };
        let exe = lib.join("steamapps").join("common").join(dir).join("x64").join("Grim Dawn.exe");
        if exe.exists() {
            return Some(exe);
        }
    }
    let default = Path::new("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Grim Dawn\\x64\\Grim Dawn.exe");
    default.exists().then(|| default.to_path_buf())
}

pub fn is_installed() -> bool {
    launcher_path().exists()
}

/// The installed version: the version.txt the packager writes into the zip ("v0.3.1" or "ci-<sha>").
pub fn installed_version() -> Option<String> {
    let text = std::fs::read_to_string(install_dir().join(VERSION_FILE)).ok()?;
    let v = text.lines().next()?.trim();
    (!v.is_empty()).then(|| v.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vdf_paths_unescape() {
        let text = "\"0\" { \"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\" }\n\"1\" { \"path\"\t\t\"D:\\\\SteamLibrary\" }";
        let v = vdf_values(text, "path");
        assert_eq!(v, vec!["C:\\Program Files (x86)\\Steam", "D:\\SteamLibrary"]);
        assert!(vdf_values(text, "installdir").is_empty());
    }
}
