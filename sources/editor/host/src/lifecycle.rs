use serde::Serialize;
use std::fmt;

pub const VIEWPORT_IPC_VERSION: &str = "kb.editor.viewport/v1";

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PhysicalSize {
    pub width: u32,
    pub height: u32,
}

impl PhysicalSize {
    pub const fn new(width: u32, height: u32) -> Self {
        Self { width, height }
    }

    pub const fn is_renderable(self) -> bool {
        self.width != 0 && self.height != 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SurfaceDimensions {
    pub physical_size: PhysicalSize,
    pub scale_factor_micros: u32,
}

impl SurfaceDimensions {
    pub fn new(physical_size: PhysicalSize, scale_factor: f64) -> Result<Self, LifecycleError> {
        if !scale_factor.is_finite() || scale_factor <= 0.0 {
            return Err(LifecycleError::InvalidScaleFactor);
        }

        let scale_factor_micros = (scale_factor * 1_000_000.0).round();
        if scale_factor_micros > u32::MAX as f64 {
            return Err(LifecycleError::InvalidScaleFactor);
        }

        Ok(Self {
            physical_size,
            scale_factor_micros: scale_factor_micros as u32,
        })
    }

    pub fn scale_factor(self) -> f64 {
        self.scale_factor_micros as f64 / 1_000_000.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum ViewportSurfaceEventKind {
    Created,
    Resized,
    Suspended,
    Resumed,
    Lost,
    Destroyed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ViewportSurfaceEvent {
    pub protocol_version: &'static str,
    pub generation: u64,
    pub sequence: u64,
    pub kind: ViewportSurfaceEventKind,
    pub physical_size: PhysicalSize,
    pub scale_factor_micros: u32,
}

impl ViewportSurfaceEvent {
    pub fn scale_factor(self) -> f64 {
        self.scale_factor_micros as f64 / 1_000_000.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SurfaceState {
    Absent,
    Ready,
    Suspended,
    Lost,
}

#[derive(Debug, Eq, PartialEq)]
pub enum LifecycleError {
    InvalidScaleFactor,
    CounterOverflow,
    AlreadyActive,
    SurfaceIsAbsent,
    SurfaceIsLost,
}

impl fmt::Display for LifecycleError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::InvalidScaleFactor => "surface scale factor must be finite and positive",
            Self::CounterOverflow => "viewport lifecycle counter overflowed",
            Self::AlreadyActive => "a viewport surface is already active",
            Self::SurfaceIsAbsent => "the viewport surface does not exist",
            Self::SurfaceIsLost => "the viewport surface is lost and must be recreated",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for LifecycleError {}

/// Deterministic state machine for a native viewport surface.
///
/// `generation` changes whenever the renderer must acquire native handles again;
/// `sequence` changes for every externally observable lifecycle event.
#[derive(Debug)]
pub struct ViewportLifecycle {
    state: SurfaceState,
    generation: u64,
    sequence: u64,
    dimensions: SurfaceDimensions,
}

impl Default for ViewportLifecycle {
    fn default() -> Self {
        Self {
            state: SurfaceState::Absent,
            generation: 0,
            sequence: 0,
            dimensions: SurfaceDimensions {
                physical_size: PhysicalSize::new(0, 0),
                scale_factor_micros: 1_000_000,
            },
        }
    }
}

impl ViewportLifecycle {
    pub fn current(&self) -> Option<ViewportSurfaceEvent> {
        (self.state != SurfaceState::Absent).then(|| self.event(ViewportSurfaceEventKind::Resized))
    }

    pub fn create_or_recreate(
        &mut self,
        dimensions: SurfaceDimensions,
    ) -> Result<ViewportSurfaceEvent, LifecycleError> {
        match self.state {
            SurfaceState::Ready | SurfaceState::Suspended => {
                return Err(LifecycleError::AlreadyActive)
            }
            SurfaceState::Absent | SurfaceState::Lost => {}
        }

        self.generation = self
            .generation
            .checked_add(1)
            .ok_or(LifecycleError::CounterOverflow)?;
        self.dimensions = dimensions;
        self.state = if dimensions.physical_size.is_renderable() {
            SurfaceState::Ready
        } else {
            SurfaceState::Suspended
        };
        self.next_event(ViewportSurfaceEventKind::Created)
    }

    pub fn resize(
        &mut self,
        dimensions: SurfaceDimensions,
    ) -> Result<Option<ViewportSurfaceEvent>, LifecycleError> {
        match self.state {
            SurfaceState::Absent => return Err(LifecycleError::SurfaceIsAbsent),
            SurfaceState::Lost => return Err(LifecycleError::SurfaceIsLost),
            SurfaceState::Ready | SurfaceState::Suspended => {}
        }

        let was_suspended = self.state == SurfaceState::Suspended;
        if self.dimensions == dimensions {
            return Ok(None);
        }

        self.dimensions = dimensions;
        if !dimensions.physical_size.is_renderable() {
            self.state = SurfaceState::Suspended;
            return (!was_suspended)
                .then(|| self.next_event(ViewportSurfaceEventKind::Suspended))
                .transpose();
        }

        self.state = SurfaceState::Ready;
        Ok(Some(self.next_event(if was_suspended {
            ViewportSurfaceEventKind::Resumed
        } else {
            ViewportSurfaceEventKind::Resized
        })?))
    }

    pub fn suspend(&mut self) -> Result<Option<ViewportSurfaceEvent>, LifecycleError> {
        match self.state {
            SurfaceState::Absent => Err(LifecycleError::SurfaceIsAbsent),
            SurfaceState::Lost => Err(LifecycleError::SurfaceIsLost),
            SurfaceState::Suspended => Ok(None),
            SurfaceState::Ready => {
                self.state = SurfaceState::Suspended;
                Ok(Some(self.next_event(ViewportSurfaceEventKind::Suspended)?))
            }
        }
    }

    pub fn resume(&mut self) -> Result<Option<ViewportSurfaceEvent>, LifecycleError> {
        match self.state {
            SurfaceState::Absent => Err(LifecycleError::SurfaceIsAbsent),
            SurfaceState::Lost => Err(LifecycleError::SurfaceIsLost),
            SurfaceState::Ready => Ok(None),
            SurfaceState::Suspended if !self.dimensions.physical_size.is_renderable() => Ok(None),
            SurfaceState::Suspended => {
                self.state = SurfaceState::Ready;
                Ok(Some(self.next_event(ViewportSurfaceEventKind::Resumed)?))
            }
        }
    }

    pub fn mark_lost(&mut self) -> Result<Option<ViewportSurfaceEvent>, LifecycleError> {
        match self.state {
            SurfaceState::Absent => Err(LifecycleError::SurfaceIsAbsent),
            SurfaceState::Lost => Ok(None),
            SurfaceState::Ready | SurfaceState::Suspended => {
                self.state = SurfaceState::Lost;
                Ok(Some(self.next_event(ViewportSurfaceEventKind::Lost)?))
            }
        }
    }

    pub fn destroy(&mut self) -> Result<Option<ViewportSurfaceEvent>, LifecycleError> {
        if self.state == SurfaceState::Absent {
            return Ok(None);
        }

        self.state = SurfaceState::Absent;
        self.next_event(ViewportSurfaceEventKind::Destroyed)
            .map(Some)
    }

    fn next_event(
        &mut self,
        kind: ViewportSurfaceEventKind,
    ) -> Result<ViewportSurfaceEvent, LifecycleError> {
        self.sequence = self
            .sequence
            .checked_add(1)
            .ok_or(LifecycleError::CounterOverflow)?;
        Ok(self.event(kind))
    }

    fn event(&self, kind: ViewportSurfaceEventKind) -> ViewportSurfaceEvent {
        ViewportSurfaceEvent {
            protocol_version: VIEWPORT_IPC_VERSION,
            generation: self.generation,
            sequence: self.sequence,
            kind,
            physical_size: self.dimensions.physical_size,
            scale_factor_micros: self.dimensions.scale_factor_micros,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dimensions(width: u32, height: u32, scale_factor: f64) -> SurfaceDimensions {
        SurfaceDimensions::new(PhysicalSize::new(width, height), scale_factor).unwrap()
    }

    #[test]
    fn lifecycle_recreates_after_surface_loss_with_a_new_generation() {
        let mut lifecycle = ViewportLifecycle::default();
        let created = lifecycle
            .create_or_recreate(dimensions(1920, 1080, 1.25))
            .unwrap();
        let lost = lifecycle.mark_lost().unwrap().unwrap();
        let recreated = lifecycle
            .create_or_recreate(dimensions(1280, 720, 1.0))
            .unwrap();

        assert_eq!(created.kind, ViewportSurfaceEventKind::Created);
        assert_eq!(lost.kind, ViewportSurfaceEventKind::Lost);
        assert_eq!(recreated.kind, ViewportSurfaceEventKind::Created);
        assert_eq!(
            (created.generation, lost.generation, recreated.generation),
            (1, 1, 2)
        );
        assert_eq!(
            (created.sequence, lost.sequence, recreated.sequence),
            (1, 2, 3)
        );
    }

    #[test]
    fn zero_extent_suspends_and_a_renderable_resize_resumes() {
        let mut lifecycle = ViewportLifecycle::default();
        lifecycle
            .create_or_recreate(dimensions(800, 600, 1.0))
            .unwrap();

        let suspended = lifecycle.resize(dimensions(0, 600, 1.0)).unwrap().unwrap();
        let resumed = lifecycle
            .resize(dimensions(800, 600, 1.5))
            .unwrap()
            .unwrap();

        assert_eq!(suspended.kind, ViewportSurfaceEventKind::Suspended);
        assert_eq!(resumed.kind, ViewportSurfaceEventKind::Resumed);
        assert_eq!(resumed.physical_size, PhysicalSize::new(800, 600));
        assert_eq!(resumed.scale_factor(), 1.5);
    }

    #[test]
    fn duplicate_resize_does_not_emit_a_redundant_event() {
        let mut lifecycle = ViewportLifecycle::default();
        lifecycle
            .create_or_recreate(dimensions(800, 600, 1.0))
            .unwrap();

        assert_eq!(lifecycle.resize(dimensions(800, 600, 1.0)).unwrap(), None);
    }

    #[test]
    fn destroyed_surface_cannot_be_resized_until_created_again() {
        let mut lifecycle = ViewportLifecycle::default();
        lifecycle
            .create_or_recreate(dimensions(800, 600, 1.0))
            .unwrap();
        let destroyed = lifecycle.destroy().unwrap().unwrap();

        assert_eq!(destroyed.kind, ViewportSurfaceEventKind::Destroyed);
        assert_eq!(
            lifecycle.resize(dimensions(1024, 768, 1.0)),
            Err(LifecycleError::SurfaceIsAbsent)
        );
    }
}
