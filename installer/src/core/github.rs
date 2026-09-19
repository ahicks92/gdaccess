//! GitHub releases of the repo: the tagged semver versions (what the picker lists, newest first) and the
//! rolling `ci-latest` pre-release that CI republishes on every push to main (offered last, on purpose).
use serde::Deserialize;

use super::paths::{CI_TAG, GITHUB_RELEASES_URL};

#[derive(Debug, Deserialize, Clone)]
pub struct ReleaseInfo {
    pub tag_name: String,
    #[serde(default)]
    pub body: String,
    #[serde(default)]
    pub prerelease: bool,
    #[serde(default)]
    pub published_at: String,
    #[serde(default)]
    pub assets: Vec<Asset>,
}

#[derive(Debug, Deserialize, Clone)]
pub struct Asset {
    pub name: String,
    pub browser_download_url: String,
}

/// What the picker offers.
#[derive(Debug, Default, Clone)]
pub struct Catalog {
    /// Semver-tagged releases, newest first.
    pub versions: Vec<ReleaseInfo>,
    /// The rolling CI build, if CI has published one.
    pub ci: Option<ReleaseInfo>,
}

impl Catalog {
    pub fn latest(&self) -> Option<&ReleaseInfo> {
        self.versions.first()
    }
}

pub fn parse_version(tag: &str) -> Option<semver::Version> {
    let t = tag.strip_prefix('v').or_else(|| tag.strip_prefix('V')).unwrap_or(tag);
    semver::Version::parse(t).ok()
}

pub fn catalog_from(releases: Vec<ReleaseInfo>) -> Catalog {
    let mut cat = Catalog::default();
    for r in releases {
        if r.tag_name == CI_TAG {
            cat.ci = Some(r);
        } else if parse_version(&r.tag_name).is_some() {
            cat.versions.push(r);
        }
    }
    cat.versions.sort_by(|a, b| parse_version(&b.tag_name).cmp(&parse_version(&a.tag_name)));
    cat
}

pub fn fetch_catalog() -> Result<Catalog, String> {
    let client = reqwest::blocking::Client::builder()
        .user_agent("GDAccessInstaller")
        .timeout(std::time::Duration::from_secs(15))
        .build()
        .map_err(|e| format!("Failed to create the HTTP client: {}", e))?;
    let resp = client.get(GITHUB_RELEASES_URL).send().map_err(|e| format!("Failed to reach GitHub: {}", e))?;
    if !resp.status().is_success() {
        return Err(format!("GitHub returned status {}", resp.status()));
    }
    let releases: Vec<ReleaseInfo> = resp.json().map_err(|e| format!("Failed to parse the release list: {}", e))?;
    Ok(catalog_from(releases))
}

/// The mod payload: the release's zip asset.
pub fn zip_asset(r: &ReleaseInfo) -> Option<&Asset> {
    r.assets.iter().find(|a| a.name.to_lowercase().ends_with(".zip"))
}

/// "v0.3.1" or "ci-latest" vs the installed version.txt: is the installed one at least as new?
pub fn is_up_to_date(installed: Option<&str>, latest: &str) -> bool {
    let Some(installed) = installed else { return false };
    match (parse_version(installed), parse_version(latest)) {
        (Some(i), Some(l)) => i >= l,
        _ => installed == latest,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(tag: &str) -> ReleaseInfo {
        ReleaseInfo { tag_name: tag.into(), body: String::new(), prerelease: false, published_at: String::new(), assets: vec![] }
    }

    #[test]
    fn catalog_sorts_and_splits() {
        let c = catalog_from(vec![rel("v0.1.0"), rel("ci-latest"), rel("v0.10.0"), rel("v0.2.0"), rel("junk")]);
        let tags: Vec<_> = c.versions.iter().map(|r| r.tag_name.as_str()).collect();
        assert_eq!(tags, vec!["v0.10.0", "v0.2.0", "v0.1.0"]);
        assert_eq!(c.ci.unwrap().tag_name, "ci-latest");
    }

    #[test]
    fn up_to_date_rules() {
        assert!(is_up_to_date(Some("v0.2.0"), "v0.2.0"));
        assert!(!is_up_to_date(Some("v0.1.9"), "v0.2.0"));
        assert!(!is_up_to_date(None, "v0.2.0"));
        assert!(!is_up_to_date(Some("ci-abc1234"), "v0.2.0"));
    }
}
