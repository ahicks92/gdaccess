//! Where things live. The mod is a self-contained folder (gdlaunch.exe, gdaccess.dll, prism.dll, assets\)
//! installed per user under %LOCALAPPDATA%\Programs\GrimDark; its log and settings stay in
//! %LOCALAPPDATA%\gdaccess, which an install or uninstall never touches. Nothing is written to the game folder.
use std::path::PathBuf;

pub const GITHUB_RELEASES_URL: &str = "https://api.github.com/repos/ahicks92/gdaccess/releases";
/// The rolling pre-release CI republishes on every push to main (the "latest successful CI build" choice).
pub const CI_TAG: &str = "ci-latest";
/// The top-level folder inside the release zip (tools/package.py).
pub const ZIP_ROOT: &str = "gdaccess";
pub const APP_NAME: &str = "GD Access";
/// The on-disk name: the install folder, the shortcuts and the Add/Remove entry. Fixed ahead of the rebrand,
/// because these are the painful ones to change once they sit on people's machines.
pub const BRAND: &str = "GrimDark";
pub const LAUNCHER_EXE: &str = "gdlaunch.exe";
pub const MOD_DLL: &str = "gdaccess.dll";
pub const INSTALLER_EXE: &str = "GDAccessInstaller.exe";
pub const VERSION_FILE: &str = "version.txt";
/// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\<this> = the Add/Remove Programs entry.
pub const UNINSTALL_KEY: &str = "GrimDark";

fn local_app_data() -> PathBuf {
    dirs::data_local_dir().unwrap_or_else(|| PathBuf::from("C:\\Users\\Default\\AppData\\Local"))
}

/// %LOCALAPPDATA%\Programs\GrimDark -- the per-user programs folder (no admin, survives anything Steam does).
pub fn install_dir() -> PathBuf {
    local_app_data().join("Programs").join(BRAND)
}

/// %LOCALAPPDATA%\gdaccess -- the mod's own log + settings.txt (kept across install/uninstall).
pub fn data_dir() -> PathBuf {
    local_app_data().join("gdaccess")
}

pub fn launcher_path() -> PathBuf {
    install_dir().join(LAUNCHER_EXE)
}

pub fn desktop_shortcut() -> Option<PathBuf> {
    dirs::desktop_dir().map(|d| d.join(format!("{}.lnk", BRAND)))
}

pub fn start_menu_shortcut() -> Option<PathBuf> {
    dirs::config_dir().map(|appdata| {
        appdata.join("Microsoft").join("Windows").join("Start Menu").join("Programs").join(format!("{}.lnk", BRAND))
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn install_dir_is_under_programs() {
        assert!(install_dir().ends_with("Programs\\GrimDark"));
        assert!(!install_dir().starts_with(data_dir()));
    }
}
