//! `--cli`: the same flow on the console (a fully keyboard path that needs no GUI at all).
use std::io::{self, Write};
use std::path::PathBuf;

use crate::core::install::Progress;
use crate::core::paths::{install_dir, APP_NAME};
use crate::core::{detect, github, install, uninstall};

pub fn run() {
    println!("=== {} installer ===", APP_NAME);
    println!();
    match detect::detect_game_exe() {
        Some(exe) => println!("Grim Dawn: {}", exe.display()),
        None => println!("Grim Dawn was not found through Steam; the launcher will need a game_path.txt (see README)."),
    }
    println!("Install folder: {}", install_dir().display());
    show_status();
    println!();
    loop {
        println!("Options:");
        println!("  1. Install / Update from GitHub");
        println!("  2. Install from a local zip file");
        println!("  3. Uninstall");
        println!("  4. Exit");
        let choice = prompt("Choose an option (1-4): ");
        println!();
        match choice.as_str() {
            "1" => install_from_github(),
            "2" => install_from_file(),
            "3" => do_uninstall(),
            "4" => return,
            _ => println!("Invalid option."),
        }
        println!();
        show_status();
        println!();
    }
}

fn show_status() {
    if detect::is_installed() {
        println!("Installed version: {}", detect::installed_version().as_deref().unwrap_or("unknown"));
    } else {
        println!("{} is not installed.", APP_NAME);
    }
}

fn prompt(msg: &str) -> String {
    print!("{}", msg);
    io::stdout().flush().ok();
    let mut s = String::new();
    io::stdin().read_line(&mut s).ok();
    s.trim().to_string()
}

fn print_progress(p: Progress) {
    match p {
        Progress::Status(s) => println!("{}", s),
        Progress::Percent(pct) if pct % 10 == 0 => println!("  {}%", pct),
        _ => {}
    }
}

fn install_from_github() {
    let cat = match github::fetch_catalog() {
        Ok(c) => c,
        Err(e) => {
            println!("Error: {}", e);
            return;
        }
    };
    if cat.versions.is_empty() && cat.ci.is_none() {
        println!("No releases are published yet.");
        return;
    }
    println!("Versions:");
    for (i, r) in cat.versions.iter().enumerate() {
        println!("  {}. {}{}", i + 1, r.tag_name, if r.prerelease { " (pre-release)" } else { "" });
    }
    let n = cat.versions.len();
    if let Some(ci) = &cat.ci {
        println!("  {}. latest successful CI build ({})", n + 1, ci.published_at);
    }
    let choice = prompt(&format!("Choose a version (1-{}), or Enter for the newest: ", n + cat.ci.iter().count()));
    let idx = if choice.is_empty() { if n > 0 { 0 } else { n } } else { choice.parse::<usize>().unwrap_or(0).saturating_sub(1) };
    let rel = if idx < n { &cat.versions[idx] } else if let Some(ci) = &cat.ci { ci } else { println!("Invalid choice."); return };
    if !rel.body.is_empty() {
        println!("\nRelease notes for {}:\n{}\n", rel.tag_name, rel.body);
    }
    let Some(asset) = github::zip_asset(rel) else {
        println!("Error: that release has no zip asset.");
        return;
    };
    if prompt(&format!("Install {}? (Y/n): ", rel.tag_name)).eq_ignore_ascii_case("n") {
        return;
    }
    report(install::download_and_install(&asset.browser_download_url, &print_progress), "installed");
}

fn install_from_file() {
    let path = PathBuf::from(prompt("Path to the Grimdark zip: "));
    if !path.exists() {
        println!("Error: no such file.");
        return;
    }
    report(install::install_from_file(&path, &print_progress), "installed");
}

fn do_uninstall() {
    if !detect::is_installed() {
        println!("{} is not installed.", APP_NAME);
        return;
    }
    if !prompt(&format!("Remove {}? Your settings are kept. (y/N): ", APP_NAME)).eq_ignore_ascii_case("y") {
        return;
    }
    report(uninstall::uninstall(), "uninstalled");
}

fn report(result: Result<(), String>, verb: &str) {
    match result {
        Ok(()) => println!("{} {} successfully.", APP_NAME, verb),
        Err(e) => println!("Error: {}", e),
    }
}
