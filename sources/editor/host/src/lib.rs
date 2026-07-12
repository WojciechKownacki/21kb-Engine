#![forbid(unsafe_code)]

mod lifecycle;
mod panel_window;

pub use lifecycle::{
    LifecycleError, PhysicalSize, SurfaceDimensions, ViewportLifecycle, ViewportSurfaceEvent,
    ViewportSurfaceEventKind, VIEWPORT_IPC_VERSION,
};
pub use panel_window::{DetachedPanelId, DetachedPanelWindowReply, PanelWindowError};

use serde::Serialize;
use std::{
    fmt,
    sync::{Arc, Mutex},
};
use tauri::{AppHandle, Emitter, Manager, State, Window, WindowEvent};

pub const SCREEN_WINDOW_LABEL: &str = "kb-editor-screen";
pub const VIEWPORT_LIFECYCLE_EVENT: &str = "kb://viewport/lifecycle/v1";

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum ViewportOpenDisposition {
    Created,
    Focused,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ViewportOpenReply {
    pub protocol_version: &'static str,
    pub disposition: ViewportOpenDisposition,
    pub surface: ViewportSurfaceEvent,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ViewportCommandError {
    pub code: &'static str,
    pub message: String,
}

#[derive(Debug)]
pub enum ViewportHostError {
    Lifecycle(LifecycleError),
    Tauri(tauri::Error),
    StatePoisoned,
    WindowIsUnavailable,
    StaleGeneration { requested: u64, current: u64 },
}

impl fmt::Display for ViewportHostError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Lifecycle(error) => error.fmt(formatter),
            Self::Tauri(error) => error.fmt(formatter),
            Self::StatePoisoned => formatter.write_str("viewport host state lock is poisoned"),
            Self::WindowIsUnavailable => {
                formatter.write_str("the Screen native window is unavailable")
            }
            Self::StaleGeneration { requested, current } => write!(
                formatter,
                "viewport generation {requested} is stale; current generation is {current}"
            ),
        }
    }
}

impl std::error::Error for ViewportHostError {}

impl From<LifecycleError> for ViewportHostError {
    fn from(error: LifecycleError) -> Self {
        Self::Lifecycle(error)
    }
}

impl From<tauri::Error> for ViewportHostError {
    fn from(error: tauri::Error) -> Self {
        Self::Tauri(error)
    }
}

impl From<ViewportHostError> for ViewportCommandError {
    fn from(error: ViewportHostError) -> Self {
        let code = match error {
            ViewportHostError::Lifecycle(_) => "viewport_lifecycle",
            ViewportHostError::Tauri(_) => "tauri_window",
            ViewportHostError::StatePoisoned => "host_state",
            ViewportHostError::WindowIsUnavailable => "screen_unavailable",
            ViewportHostError::StaleGeneration { .. } => "stale_generation",
        };
        Self {
            code,
            message: error.to_string(),
        }
    }
}

impl From<tauri::Error> for ViewportCommandError {
    fn from(error: tauri::Error) -> Self {
        Self {
            code: "tauri_window",
            message: error.to_string(),
        }
    }
}

/// Owns lifecycle state for the one native, webview-free Screen window.
///
/// Register one instance with `Builder::manage(ViewportHost::new())` and expose
/// `viewport_open_screen` through the application's invoke handler.
#[derive(Clone, Default)]
pub struct ViewportHost {
    lifecycle: Arc<Mutex<ViewportLifecycle>>,
}

impl ViewportHost {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn open_or_focus(&self, app: &AppHandle) -> Result<ViewportOpenReply, ViewportHostError> {
        if let Some(window) = app.get_window(SCREEN_WINDOW_LABEL) {
            window.show()?;
            window.set_focus()?;
            let surface = self.reconcile_existing_window(&window)?;
            return Ok(ViewportOpenReply {
                protocol_version: VIEWPORT_IPC_VERSION,
                disposition: ViewportOpenDisposition::Focused,
                surface,
            });
        }

        let window = tauri::window::WindowBuilder::new(app, SCREEN_WINDOW_LABEL)
            .title("21kb Engine — Screen")
            .inner_size(1280.0, 720.0)
            .min_inner_size(480.0, 320.0)
            .resizable(true)
            .visible(true)
            .build()?;

        self.install_window_listener(app.clone(), &window);
        let surface = self.create_for_window(&window)?;
        self.emit_lifecycle(app, surface)?;

        Ok(ViewportOpenReply {
            protocol_version: VIEWPORT_IPC_VERSION,
            disposition: ViewportOpenDisposition::Created,
            surface,
        })
    }

    /// Runs the Vulkan-surface creation closure while the native window and the
    /// requested lifecycle generation are known to be live. Raw platform handles
    /// never cross the JSON IPC boundary.
    pub fn with_native_window<T>(
        &self,
        app: &AppHandle,
        generation: u64,
        create_surface: impl FnOnce(&Window) -> Result<T, ViewportHostError>,
    ) -> Result<T, ViewportHostError> {
        let current = self.current_generation()?;
        if current != generation {
            return Err(ViewportHostError::StaleGeneration {
                requested: generation,
                current,
            });
        }

        let window = app
            .get_window(SCREEN_WINDOW_LABEL)
            .ok_or(ViewportHostError::WindowIsUnavailable)?;
        create_surface(&window)
    }

    /// Called by the renderer after `VK_ERROR_SURFACE_LOST_KHR`. The renderer must
    /// destroy its Vulkan surface and call `create_or_recreate` before presenting again.
    pub fn report_surface_lost(
        &self,
        app: &AppHandle,
        generation: u64,
    ) -> Result<(), ViewportHostError> {
        let event = {
            let mut lifecycle = self.lock_lifecycle()?;
            if lifecycle
                .current()
                .map(|event| event.generation)
                .unwrap_or_default()
                != generation
            {
                return Err(ViewportHostError::StaleGeneration {
                    requested: generation,
                    current: lifecycle
                        .current()
                        .map(|event| event.generation)
                        .unwrap_or_default(),
                });
            }
            lifecycle.mark_lost()?
        };

        if let Some(event) = event {
            self.emit_lifecycle(app, event)?;
        }
        Ok(())
    }

    /// Recreates the host lifecycle generation after the renderer has destroyed
    /// resources for a lost surface. The renderer acquires fresh native handles
    /// through `with_native_window` using the returned generation.
    pub fn recreate_surface(
        &self,
        app: &AppHandle,
        generation: u64,
    ) -> Result<ViewportSurfaceEvent, ViewportHostError> {
        let current = self.current_generation()?;
        if current != generation {
            return Err(ViewportHostError::StaleGeneration {
                requested: generation,
                current,
            });
        }

        let window = app
            .get_window(SCREEN_WINDOW_LABEL)
            .ok_or(ViewportHostError::WindowIsUnavailable)?;
        let event = self.create_for_window(&window)?;
        self.emit_lifecycle(app, event)?;
        Ok(event)
    }

    fn install_window_listener(&self, app: AppHandle, window: &Window) {
        let host = self.clone();
        window.on_window_event(move |event| {
            let lifecycle_event = match host.reduce_window_event(event) {
                Ok(event) => event,
                Err(error) => {
                    eprintln!("viewport lifecycle transition failed: {error}");
                    return;
                }
            };

            if let Some(lifecycle_event) = lifecycle_event {
                if let Err(error) = host.emit_lifecycle(&app, lifecycle_event) {
                    eprintln!("viewport lifecycle IPC emission failed: {error}");
                }
            }
        });
    }

    fn reduce_window_event(
        &self,
        event: &WindowEvent,
    ) -> Result<Option<ViewportSurfaceEvent>, ViewportHostError> {
        let mut lifecycle = self.lock_lifecycle()?;
        match event {
            WindowEvent::Resized(size) => {
                let scale_factor = lifecycle
                    .current()
                    .map(|event| event.scale_factor())
                    .unwrap_or(1.0);
                lifecycle
                    .resize(dimensions_from_parts(
                        size.width,
                        size.height,
                        scale_factor,
                    )?)
                    .map_err(Into::into)
            }
            WindowEvent::ScaleFactorChanged {
                scale_factor,
                new_inner_size,
                ..
            } => lifecycle
                .resize(dimensions_from_parts(
                    new_inner_size.width,
                    new_inner_size.height,
                    *scale_factor,
                )?)
                .map_err(Into::into),
            WindowEvent::Focused(false) => lifecycle.suspend().map_err(Into::into),
            WindowEvent::Focused(true) => lifecycle.resume().map_err(Into::into),
            WindowEvent::Destroyed => lifecycle.destroy().map_err(Into::into),
            _ => Ok(None),
        }
    }

    fn create_for_window(
        &self,
        window: &Window,
    ) -> Result<ViewportSurfaceEvent, ViewportHostError> {
        let dimensions = dimensions_from_window(window)?;
        self.lock_lifecycle()?
            .create_or_recreate(dimensions)
            .map_err(Into::into)
    }

    fn reconcile_existing_window(
        &self,
        window: &Window,
    ) -> Result<ViewportSurfaceEvent, ViewportHostError> {
        let dimensions = dimensions_from_window(window)?;
        let mut lifecycle = self.lock_lifecycle()?;
        match lifecycle.current() {
            None => lifecycle.create_or_recreate(dimensions).map_err(Into::into),
            Some(current) if current.kind == ViewportSurfaceEventKind::Lost => Ok(current),
            Some(current) => {
                if let Some(event) = lifecycle.resize(dimensions)? {
                    Ok(event)
                } else {
                    Ok(current)
                }
            }
        }
    }

    fn current_generation(&self) -> Result<u64, ViewportHostError> {
        self.lock_lifecycle()?
            .current()
            .map(|event| event.generation)
            .ok_or(ViewportHostError::WindowIsUnavailable)
    }

    fn emit_lifecycle(
        &self,
        app: &AppHandle,
        event: ViewportSurfaceEvent,
    ) -> Result<(), ViewportHostError> {
        app.emit(VIEWPORT_LIFECYCLE_EVENT, event)?;
        Ok(())
    }

    fn lock_lifecycle(
        &self,
    ) -> Result<std::sync::MutexGuard<'_, ViewportLifecycle>, ViewportHostError> {
        self.lifecycle
            .lock()
            .map_err(|_| ViewportHostError::StatePoisoned)
    }
}

fn dimensions_from_window(window: &Window) -> Result<SurfaceDimensions, ViewportHostError> {
    let size = window.inner_size()?;
    dimensions_from_parts(size.width, size.height, window.scale_factor()?).map_err(Into::into)
}

fn dimensions_from_parts(
    width: u32,
    height: u32,
    scale_factor: f64,
) -> Result<SurfaceDimensions, LifecycleError> {
    SurfaceDimensions::new(PhysicalSize::new(width, height), scale_factor)
}

/// Async is intentional: Tauri documents synchronous native window creation in a
/// command as deadlocking on Windows with WebView2.
mod commands {
    use super::*;

    #[tauri::command]
    pub async fn viewport_open_screen(
        app: AppHandle,
        viewport_host: State<'_, ViewportHost>,
    ) -> Result<ViewportOpenReply, ViewportCommandError> {
        viewport_host.open_or_focus(&app).map_err(Into::into)
    }

    /// Opens a detached editor panel as a WebView window. Game View is intentionally
    /// absent: it is represented only by the native `viewport_open_screen` path.
    #[tauri::command(rename_all = "camelCase")]
    pub async fn editor_open_panel_window(
        app: AppHandle,
        panel_id: String,
    ) -> Result<DetachedPanelWindowReply, ViewportCommandError> {
        let panel_id = DetachedPanelId::parse(&panel_id)?;
        let label = panel_id.window_label();

        if let Some(window) = app.get_webview_window(label) {
            window.show()?;
            window.set_focus()?;
            return Ok(DetachedPanelWindowReply {
                panel_id,
                window_label: label,
                disposition: panel_window::DetachedPanelWindowDisposition::Focused,
            });
        }

        let route = format!("index.html?panel={}", panel_id.route_panel_id());
        let (minimum_width, minimum_height) = panel_id.minimum_size();
        tauri::WebviewWindowBuilder::new(&app, label, tauri::WebviewUrl::App(route.into()))
            .title(panel_id.title())
            .inner_size(480.0, 640.0)
            .min_inner_size(minimum_width, minimum_height)
            .resizable(true)
            .build()?;

        Ok(DetachedPanelWindowReply {
            panel_id,
            window_label: label,
            disposition: panel_window::DetachedPanelWindowDisposition::Created,
        })
    }
}

pub use commands::{editor_open_panel_window, viewport_open_screen};
