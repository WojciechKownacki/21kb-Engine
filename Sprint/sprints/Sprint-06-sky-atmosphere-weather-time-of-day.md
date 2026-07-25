# Sprint 06 · Sky / Atmosphere / Weather / Time-of-Day

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver an integrated atmospheric simulation and authoring system for sky, celestial bodies, clouds, weather, fog, wind, lighting, world response, audio, scripting, and scalable quality tiers.

## Sky & Atmosphere Model

- [ ] Add a physically based atmospheric-scattering sky
- [ ] Add Rayleigh scattering with configurable coefficients
- [ ] Add Mie scattering with configurable coefficients and anisotropy
- [ ] Add a sky-view lookup table for cheap full-screen sky evaluation
- [ ] Add a transmittance lookup table
- [ ] Add aerial perspective applied to distant geometry
- [ ] Add multiple-scattering approximation for realistic daylight
- [ ] Add planet curvature and configurable atmosphere height
- [ ] Add ground-albedo influence on sky and horizon color
- [ ] Add ozone absorption for accurate blue and sunset tones
- [ ] Add altitude and air-density controls
- [ ] Add a fast analytic sky option for low-end hardware
- [ ] Add a captured/HDR sky option with runtime tint and rotation
- [ ] Add automatic sky-light and ambient capture from the atmosphere
- [ ] Add horizon haze and blending into distance fog
- [ ] Add debug visualization of scattering and lookup tables

## Sun, Moon & Celestial Bodies

- [ ] Add a sun disk with adjustable size and intensity
- [ ] Add sun limb darkening
- [ ] Add a moon with orientation and surface texture
- [ ] Add moon phase computation and rendering
- [ ] Add astronomically correct sun position by date, time, and latitude
- [ ] Add astronomically correct moon position
- [ ] Add manual sun and moon positioning for art-directed skies
- [ ] Add earthshine and moonlight contribution at night
- [ ] Add support for multiple suns or custom celestial light sources
- [ ] Add solar and lunar eclipse handling
- [ ] Add a sun-glow and bloom response tied to intensity
- [ ] Add a lens-flare response for the sun

## Stars & Night Sky

- [ ] Add a star field with realistic brightness distribution
- [ ] Add a milky-way band and deep-sky background
- [ ] Add constellation and named-star placement
- [ ] Add visible planets positioned by date and time
- [ ] Add star twinkle
- [ ] Add atmospheric extinction of stars near the horizon
- [ ] Add shooting stars and meteor showers
- [ ] Add star rotation synchronized with the day/night cycle
- [ ] Add auroras for polar and stylized skies
- [ ] Add a night-sky brightness and light-pollution control

## Volumetric Clouds

- [ ] Add ray-marched volumetric clouds with layered coverage
- [ ] Add cloud types (cumulus, stratus, cirrus) driven by presets
- [ ] Add cloud coverage control
- [ ] Add cloud density and altitude controls
- [ ] Add cloud lighting with multiple scattering
- [ ] Add silver-lining and powder terms for realism
- [ ] Add cloud shadows cast onto the world
- [ ] Add wind-driven cloud movement and evolution over time
- [ ] Add weather-driven coverage that thickens before storms
- [ ] Add cheap 2D cloud-plane fallback for low-end hardware
- [ ] Add temporal reprojection to keep cloud cost low
- [ ] Add cloud-shape authoring from noise and profile curves
- [ ] Add horizon and high-altitude cloud layers
- [ ] Add quality presets scaling ray-march steps and resolution
- [ ] Add cloud rendering into reflections and distant views
- [ ] Add cloud cost budgets and diagnostics

## Time of Day

- [ ] Add a day/night cycle with adjustable day length
- [ ] Add a time-of-day value driving sun, moon, sky, and lighting
- [ ] Add pause, scrub, and playback-speed control of time
- [ ] Add date, season, and latitude inputs
- [ ] Add keyframed sky and lighting profiles across the day
- [ ] Add smooth interpolation between time-of-day keyframes
- [ ] Add sunrise, noon, sunset, and night presets
- [ ] Add golden-hour and blue-hour tuning
- [ ] Add season-driven sun path and day-length changes
- [ ] Add scripting hooks for time events (dawn, dusk, midnight)
- [ ] Add save and restore of the current time state
- [ ] Add a time-of-day timeline editor with a 24-hour track

## Weather System

- [ ] Add a weather-state model (clear, cloudy, rain, storm, snow, fog)
- [ ] Add smooth transitions and blending between weather states
- [ ] Add a weather timeline and scheduler
- [ ] Add randomized and seeded weather sequences
- [ ] Add localized weather zones with falloff
- [ ] Add intensity control per weather state
- [ ] Add storm build-up with darkening sky and rising wind
- [ ] Add lightning generation
- [ ] Add thunder with distance-based delay
- [ ] Add weather presets and a preset blending system
- [ ] Add gameplay and scripting hooks for weather changes
- [ ] Add save and restore of the current weather state
- [ ] Add climate profiles that bias weather probability by region
- [ ] Add deterministic weather for replays and networked sessions

## Precipitation & Accumulation

- [ ] Add rain with adjustable density, speed, and angle
- [ ] Add snow with drift and settling behavior
- [ ] Add hail and sleet variants
- [ ] Add rain splashes and ripples on surfaces
- [ ] Add rain interaction with water surfaces
- [ ] Add camera-relative precipitation that follows the view
- [ ] Add occlusion so precipitation stops under cover
- [ ] Add surface wetness that builds and dries over time
- [ ] Add puddle formation in low areas during rain
- [ ] Add snow accumulation on upward-facing surfaces
- [ ] Add ice and frost formation in cold conditions
- [ ] Add gradual melt and evaporation as weather clears
- [ ] Add wind influence on precipitation direction
- [ ] Add precipitation particle budgets and quality scaling

## Fog & Atmospheric Effects

- [ ] Add exponential height fog with color and density controls
- [ ] Add volumetric fog with light scattering
- [ ] Add light shafts and god rays from the sun
- [ ] Add light shafts from local lights
- [ ] Add ground mist and low-lying fog banks
- [ ] Add fog color driven by time of day and sky
- [ ] Add distance and depth fog blended with aerial perspective
- [ ] Add localized fog volumes with falloff
- [ ] Add heat-haze and shimmer effects
- [ ] Add fog quality scaling and cost budgets

## Lighting Integration

- [ ] Drive the main directional light from the sun position
- [ ] Drive a secondary directional light from the moon at night
- [ ] Update sky-light and ambient from the current atmosphere on change
- [ ] Add color-temperature shifts across the day
- [ ] Add automatic exposure adaptation across day and night
- [ ] Add cloud and weather dimming of direct light
- [ ] Add lightning flashes as transient scene lighting
- [ ] Update global illumination when the sky changes significantly
- [ ] Add night artificial-light response (streetlights on at dusk)
- [ ] Add shadow color and softness tied to sky conditions
- [ ] Add throttled sky-light updates to control cost

## Wind Integration

- [ ] Add a global wind driven by weather and time
- [ ] Add gusts and turbulence that ramp with storms
- [ ] Add wind direction changes over time
- [ ] Add wind zones with local overrides
- [ ] Expose wind to foliage from one shared source
- [ ] Expose wind to cloth and particles from the same source
- [ ] Expose wind to clouds from the same source
- [ ] Add wind strength visualization and debug readout

## Weather Effects on the World

- [ ] Add dynamic surface wetness response in materials
- [ ] Add snow material response tied to accumulation
- [ ] Add ice material response tied to freezing
- [ ] Add puddle reflections and ripple response
- [ ] Add lightning strike points with world impact and light
- [ ] Add wind-driven debris and leaves during storms
- [ ] Add temperature as a shared value driving snow, ice, and melt
- [ ] Add weather influence on foliage color and health

## Audio Integration

- [ ] Add ambient rain and storm soundscapes
- [ ] Add ambient wind soundscapes
- [ ] Add thunder synchronized with lightning and distance
- [ ] Add smooth audio transitions between weather states
- [ ] Add interior and sheltered attenuation of weather audio
- [ ] Add time-of-day ambience (birds at dawn, crickets at night)
- [ ] Add audio intensity tied to weather strength

## Authoring & Presets

- [ ] Add a time-of-day keyframe editor with a day timeline
- [ ] Add a sky and atmosphere preset library
- [ ] Add a weather preset library with blend weights
- [ ] Add curve-based control of color over time
- [ ] Add curve-based control of intensity and density over time
- [ ] Add climate presets that bundle sky, weather, and lighting
- [ ] Add capture of the current look into a reusable preset
- [ ] Add layering of art-directed overrides on top of physical simulation
- [ ] Add copy and share of sky and weather presets
- [ ] Add preset thumbnails and categories

## User-Friendly Authoring

- [ ] Add a simple time-of-day slider with live preview
- [ ] Add one-click weather buttons (clear, rain, storm, snow, fog)
- [ ] Add plain-language sliders (cloudiness, wind, wetness, warmth)
- [ ] Add sensible defaults that produce a good sky immediately
- [ ] Add a beginner mode that hides physical parameters
- [ ] Add "make it dramatic" and "make it calm" one-click looks
- [ ] Add a scrubbable day preview to see the sky across 24 hours
- [ ] Add before/after compare for preset changes
- [ ] Add hover hints and a short guided tour
- [ ] Add a gallery of example skies and weather to open and tweak
- [ ] Add always-available undo and redo for every change

## Data-Driven & Scripting

- [ ] Add a weather-schedule asset for scripted campaigns
- [ ] Add events for weather changes consumable by gameplay
- [ ] Add events for time-of-day milestones consumable by gameplay
- [ ] Add a scripting API to query and set time
- [ ] Add a scripting API to query and set weather and wind
- [ ] Add conditional weather triggered by location or gameplay state
- [ ] Add deterministic weather for replays and networked sessions
- [ ] Add persistence of full sky and weather state in saves

## Performance & Quality Scaling

- [ ] Add quality presets scaling sky, cloud, and fog cost
- [ ] Add lookup-table caching for sky and aerial perspective
- [ ] Add temporal amortization of expensive atmosphere work
- [ ] Add temporal amortization of expensive cloud work
- [ ] Add resolution scaling for volumetric passes
- [ ] Add budgets and diagnostics for sky, cloud, and weather cost
- [ ] Add automatic downscaling to hold target framerate

## Testing & Validation

- [ ] Add golden-image tests across representative times of day
- [ ] Add golden-image tests across weather states
- [ ] Add weather-transition determinism tests
- [ ] Add sky lookup-table validation
- [ ] Add sun-position accuracy tests by time, date, and latitude
- [ ] Add moon-phase and moon-position accuracy tests
- [ ] Add save/restore fidelity tests for sky and weather state
- [ ] Add performance budget tests for volumetric passes
