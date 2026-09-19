//! Uninstall = the install folder, the two shortcuts and the Add/Remove entry. The mod's data folder
//! (%LOCALAPPDATA%\Grimdark: log + settings.txt) is kept for a future reinstall.
use std::fs;

use super::paths::install_dir;
use super::shortcuts;

pub fn uninstall() -> Result<(), String> {
    let dir = install_dir();
    shortcuts::remove_all();
    if dir.exists() {
        fs::remove_dir_all(&dir).map_err(|e| {
            format!("Could not remove {} -- close Grimdark and the game if they are running, then try again. ({})", dir.display(), e)
        })?;
    }
    Ok(())
}
