use serde::Serialize;
use std::fmt;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum DetachedPanelId {
    ProjectFiles,
    SceneObjects,
    Details,
    Console,
}

impl DetachedPanelId {
    pub fn parse(value: &str) -> Result<Self, PanelWindowError> {
        match value {
            "projectFiles" => Ok(Self::ProjectFiles),
            "sceneObjects" => Ok(Self::SceneObjects),
            "details" => Ok(Self::Details),
            "console" => Ok(Self::Console),
            _ => Err(PanelWindowError::UnsupportedPanel(value.to_owned())),
        }
    }

    pub const fn window_label(self) -> &'static str {
        match self {
            Self::ProjectFiles => "kb-editor-panel-project-files",
            Self::SceneObjects => "kb-editor-panel-scene-objects",
            Self::Details => "kb-editor-panel-details",
            Self::Console => "kb-editor-panel-console",
        }
    }

    pub const fn title(self) -> &'static str {
        match self {
            Self::ProjectFiles => "21kb Engine — Project Files",
            Self::SceneObjects => "21kb Engine — Scene Objects",
            Self::Details => "21kb Engine — Details",
            Self::Console => "21kb Engine — Console",
        }
    }

    pub const fn route_panel_id(self) -> &'static str {
        match self {
            Self::ProjectFiles => "project-files",
            Self::SceneObjects => "scene-objects",
            Self::Details => "details",
            Self::Console => "console",
        }
    }

    pub const fn minimum_size(self) -> (f64, f64) {
        match self {
            Self::ProjectFiles => (220.0, 180.0),
            Self::SceneObjects => (240.0, 180.0),
            Self::Details => (260.0, 220.0),
            Self::Console => (360.0, 160.0),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DetachedPanelWindowReply {
    pub panel_id: DetachedPanelId,
    pub window_label: &'static str,
    pub disposition: DetachedPanelWindowDisposition,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum DetachedPanelWindowDisposition {
    Created,
    Focused,
}

#[derive(Debug)]
pub enum PanelWindowError {
    UnsupportedPanel(String),
    Tauri(tauri::Error),
}

impl fmt::Display for PanelWindowError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedPanel(panel_id) => {
                write!(formatter, "panel '{panel_id}' cannot be detached")
            }
            Self::Tauri(error) => error.fmt(formatter),
        }
    }
}

impl std::error::Error for PanelWindowError {}

impl From<tauri::Error> for PanelWindowError {
    fn from(error: tauri::Error) -> Self {
        Self::Tauri(error)
    }
}

impl From<PanelWindowError> for super::ViewportCommandError {
    fn from(error: PanelWindowError) -> Self {
        let code = match error {
            PanelWindowError::UnsupportedPanel(_) => "unsupported_panel",
            PanelWindowError::Tauri(_) => "tauri_window",
        };
        Self {
            code,
            message: error.to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_webview_panels_have_stable_detached_window_labels() {
        assert_eq!(
            DetachedPanelId::parse("projectFiles")
                .unwrap()
                .window_label(),
            "kb-editor-panel-project-files"
        );
        assert_eq!(
            DetachedPanelId::parse("console").unwrap().title(),
            "21kb Engine — Console"
        );
        assert_eq!(
            DetachedPanelId::parse("sceneObjects")
                .unwrap()
                .route_panel_id(),
            "scene-objects"
        );
    }

    #[test]
    fn game_view_and_unknown_ids_are_rejected() {
        assert!(matches!(
            DetachedPanelId::parse("gameView"),
            Err(PanelWindowError::UnsupportedPanel(_))
        ));
        assert!(matches!(
            DetachedPanelId::parse("unknown"),
            Err(PanelWindowError::UnsupportedPanel(_))
        ));
    }

    #[test]
    fn detached_window_minimums_preserve_panel_contracts() {
        assert_eq!(DetachedPanelId::ProjectFiles.minimum_size(), (220.0, 180.0));
        assert_eq!(DetachedPanelId::Console.minimum_size(), (360.0, 160.0));
    }
}
