// MicrosoftGame.config (the GDK title's manifest at the package root): identity and executables.

use std::path::{Component, Path, PathBuf};

pub struct GameConfig {
    pub name: Option<String>,
    pub version: Option<String>,
    pub display_name: Option<String>,
    pub executables: Vec<PathBuf>,
}

// A relative path that stays inside the package.
fn inside(name: &str) -> Option<PathBuf> {
    let path = PathBuf::from(name.replace('/', "\\"));
    path.components()
        .all(|c| matches!(c, Component::Normal(_)))
        .then_some(path)
}

pub fn parse(text: &str) -> Option<GameConfig> {
    let doc = roxmltree::Document::parse(text.trim_start_matches('\u{feff}')).ok()?;
    let root = doc.root_element();
    if !root.has_tag_name("Game") {
        return None;
    }
    let identity = root.children().find(|n| n.has_tag_name("Identity"));
    let visuals = root.children().find(|n| n.has_tag_name("ShellVisuals"));
    let mut executables = Vec::new();
    if let Some(list) = root.children().find(|n| n.has_tag_name("ExecutableList")) {
        for exe in list.children().filter(|n| n.has_tag_name("Executable")) {
            let family = exe.attribute("TargetDeviceFamily").unwrap_or("PC");
            if !family.eq_ignore_ascii_case("PC") {
                continue;
            }
            if let Some(path) = exe.attribute("Name").and_then(inside) {
                executables.push(path);
            }
        }
    }
    Some(GameConfig {
        name: identity.and_then(|i| i.attribute("Name")).map(str::to_owned),
        version: identity.and_then(|i| i.attribute("Version")).map(str::to_owned),
        display_name: visuals
            .and_then(|v| v.attribute("DefaultDisplayName"))
            .filter(|n| !n.starts_with("ms-resource:"))
            .map(str::to_owned),
        executables,
    })
}

pub fn read(install: &Path) -> Option<GameConfig> {
    parse(&std::fs::read_to_string(install.join("MicrosoftGame.config")).ok()?)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn picks_pc_executables_inside_the_package() {
        let config = parse(
            r#"<?xml version="1.0"?><Game configVersion="1"><Identity Name="Studio.Game" Publisher="CN=x" Version="1.2.3.0"/>
            <ExecutableList><Executable Name="Bin\Game.exe" TargetDeviceFamily="PC" Id="Game"/>
            <Executable Name="..\evil.exe" Id="Evil"/><Executable Name="Console.exe" TargetDeviceFamily="Scarlett"/></ExecutableList>
            <ShellVisuals DefaultDisplayName="The Game"/></Game>"#,
        )
        .unwrap();
        assert_eq!(config.name.as_deref(), Some("Studio.Game"));
        assert_eq!(config.version.as_deref(), Some("1.2.3.0"));
        assert_eq!(config.display_name.as_deref(), Some("The Game"));
        assert_eq!(config.executables, vec![PathBuf::from("Bin\\Game.exe")]);
    }
}
