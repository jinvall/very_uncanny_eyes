# Build Roadmap

## Goal

Ship the next major release of Uncanny Eyes as a realistic desktop eye-display system with a proper expression model, deterministic pupil mapping, tightened light-response behavior, and a clean 16:9 remote-display pipeline.

This roadmap is intentionally scoped to three phases. Each phase produces a testable milestone instead of a vague "big rewrite".

## Design constraints

- Desktop-first eye display, not embedded sketch code.
- Borderless 16:9 display output only for remote stream capture and projection.
- Dozing already exists; the next phase adds explicit suspicious and angry behavior.
- LDR input remains the primary environmental driver, but it must be tightened and converted into a better physiological response model.
- 200x200 normalized eyeball + deterministic pupil mapping is the target path for realistic, non-round pupils.
- Generated eye headers are large (~16k lines per eyeball asset). Do not hand-edit generated tables; add a generation path and keep asset source of truth outside the generated output.

---

## Phase 1 — Foundation: mode architecture and render pipeline

### Objective

Stabilize the runtime architecture so expression modes and eye behavior are modeled as first-class systems, not ad hoc tweaks.

### Scope

- Define a mode registry: normal, dozing, suspicious, angry.
- Introduce an expression profile and state model that separates:
  - gaze
  - eyelid motion
  - pupil dilation
  - head tilt / micro-motion
  - blink rhythm
- Consolidate the animation loop around a clean behavior pipeline without hardcoding logic inside the renderer.
- Verify the existing dozing behavior remains stable while the architecture expands.
- Keep generated eyeball data isolated and generator-driven so the project does not become unmaintainable.

### Exit criteria

- App starts from a consistent mode enum and profile object.
- Suspicious and angry modes are represented in config and runtime state.
- Existing dozing behavior still works without regressions.
- Renderer remains deterministic for smoke tests and real-time visual validation.

---

## Phase 2 — Realistic eye system: pupil mapping and iris behavior

### Objective

Replace the older iris-strip assumptions with a more realistic eye-generation pipeline built around a normalized 200x200 eyeball and deterministic pupil extraction.

### Scope

- Build a normalized eyeball asset format around 200x200 eye plate and `pupilMap` semantics.
- Use the black crosshair approach to detect a non-round pupil reliably, especially for narrow slitted pupils (cat, reptilian, goat, etc.).
- Add a deterministic pupil radius mapping from the normalized marker to screen-space shape.
- Refine dilation response and constriction timing so the pupil behavior feels physiological instead of raw ADC-driven.
- Support a future iris type registry: default, slit, reptilian, cat, goat, realistic mammalian.
- Keep the pupil mapping non-round-safe even when the pupil is not perfectly circular.

### Exit criteria

- Pupil center and radius are deterministic under a fixed normalized input.
- Non-round pupils render without the black hole escaping the actual pupil boundaries.
- Dilation response is smoother and less twitchy under changing light.
- Eye profile switching is possible without rewiring the renderer.

---

## Phase 3 — Desktop integration, streaming, and production polish

### Objective

Turn the project into a polished desktop display system that is comfortable in a real installation: remote display, borderless output, and clean operator controls.

### Scope

- Keep output to borderless 16:9 only for stream-friendly display.
- Tighten the RTSP/VLC pipeline and window capture behavior.
- Add an operator TUI and launch controls for runtime mode switching, design selection, and light calibration.
- Add runtime tuning for light response, pupil curve, expression strength, and animation smoothing.
- Prepare support for future feature flags: iris type, pupil type, expression type, realism level.
- Document the asset-generation workflow and keep generated files under a disciplined pipeline.

### Exit criteria

- Remote display is stable at 16:9 with minimal light pollution.
- TUI allows mode switching and calibration without editor-level downtime.
- Eye behavior remains stable under live use and streaming.
- The system is ready for the next realism pass without reworking the core architecture.

---

## Milestone sequencing

1. Foundation first: make behavior modes, profiles, and runtime state correct.
2. Realism second: fix the actual eye and pupil math to match the target biological behavior.
3. Polish last: get the display and operator UX into a production-friendly state.

This order minimizes churn: the renderer and behavior system must be sound before the new eye assets and tuning rules are introduced.
