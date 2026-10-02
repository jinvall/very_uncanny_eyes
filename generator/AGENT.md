# PNG → Adafruit Uncanny Eyes Asset Generator

## Project Overview

Build a desktop tool that accepts a single PNG image of an eye and automatically converts it into the separate graphical assets and configuration data required to create a new eye design for Adafruit's **Uncanny Eyes** project.

Reference project:

https://github.com/adafruit/Uncanny_Eyes

The goal is:

**INPUT**

`my_eye.png`

↓

**ANALYSIS / SEGMENTATION**

Identify the visual components of the eye and determine their geometry, colors, transparency, and relationships.

↓

**GENERATED ASSETS**

Create the individual eye layers/masks required by the Uncanny Eyes rendering system.

↓

**OUTPUT**

A complete, compilable Uncanny Eyes eye-design package that can be dropped into the project and rendered by the existing eye renderer.

The tool should automate as much of this process as possible while providing a preview/editor for correcting automatically detected boundaries.

---

# 1. Primary User Workflow

The intended workflow should be extremely simple:

1. Launch application.
2. Drop a PNG onto the application.
3. Application analyzes the image.
4. Application displays the detected eye structure.
5. Application generates proposed layers.
6. User can inspect/tweak the detected regions.
7. Application generates:

   * individual image assets
   * transparency masks where required
   * palette/color information
   * dimensions
   * positioning information
   * Uncanny Eyes-compatible graphics header/data
   * configuration/source files needed to add the eye to the project
8. Application provides a preview showing the eye rendered using the actual Uncanny Eyes renderer logic as closely as practical.

The user should NOT have to manually cut the eye into pieces in an image editor.

---

# 2. Input Requirements

Accept:

* PNG
* RGB PNG
* RGBA PNG
* indexed PNG where practical
* arbitrary reasonable resolution

Primary intended input:

A relatively straight-on image of a single eye.

The input may contain:

* sclera
* iris
* pupil
* eyelids
* eyelashes
* highlights
* shadows
* gradients
* irregular iris shapes
* irregular pupil shapes
* colored irises
* non-human/fantasy eyes

Do NOT assume the eye is perfectly circular.

Do NOT assume the iris is centered.

Do NOT assume the pupil is centered in the iris.

Do NOT assume the sclera is pure white.

---

# 3. Initial Image Analysis

Automatically determine:

## 3.1 Eye boundary

Detect the visible eye/eyelid region.

Generate:

`eye_mask`

This defines the visible area of the eye.

The mask should preserve:

* upper eyelid boundary
* lower eyelid boundary
* corners
* eyelashes when they are visually part of the eye

Avoid simply creating a rectangular crop.

---

# 4. Identify Visual Components

Attempt to identify the following independently:

### Layer 1 — Sclera

The visible white/outer eye region.

Output:

`sclera.png`

and:

`sclera_mask.png`

The sclera should retain the original shading rather than being flattened to white.

---

### Layer 2 — Iris

Detect the iris boundary.

Output:

`iris.png`

and:

`iris_mask.png`

The iris should preserve:

* radial texture
* color variation
* limbal ring
* irregular edges
* highlights if they are determined to belong to the iris

The iris should be represented in a way that permits the Uncanny Eyes renderer to reposition it dynamically.

---

### Layer 3 — Pupil

Detect the pupil separately.

Output:

`pupil.png`

and:

`pupil_mask.png`

Support:

* circular pupils
* elliptical pupils
* slit pupils
* irregular pupils

Do NOT force all pupils into circles.

---

### Layer 4 — Highlights

Attempt to detect specular highlights/reflections.

Output:

`highlight.png`

and:

`highlight_mask.png`

Highlights should preferably remain independently renderable so that the pupil/iris can move underneath them.

---

### Layer 5 — Eyelid / Occlusion Mask

Generate an occlusion mask describing the portion of the eye that should appear in front of the moving iris.

Output:

`eyelid_mask.png`

This is important because the iris should not simply move over the entire rectangular image.

The moving eye should be clipped by the eyelid geometry.

---

### Layer 6 — Eyelashes / Fine Detail

Where identifiable, isolate eyelashes and other foreground details.

Output:

`foreground.png`

and:

`foreground_mask.png`

This layer should remain stationary while the underlying eyeball components move.

---

# 5. Coordinate System

Create a normalized coordinate system for the eye.

Determine:

* overall eye bounding box
* eye center
* iris center
* iris radius / horizontal and vertical radius
* pupil center
* pupil dimensions
* highlight locations
* eyelid boundaries
* maximum iris travel region

Store these values in a machine-readable configuration file.

Example:

```json
{
    "eye": {
        "width": 320,
        "height": 180,
        "center": [160, 90]
    },
    "iris": {
        "center": [162, 91],
        "radius_x": 52,
        "radius_y": 52
    },
    "pupil": {
        "center": [162, 91],
        "radius_x": 19,
        "radius_y": 19
    }
}
```

Do not assume these exact fields are sufficient. Expand the format as necessary.

---

# 6. Perspective / Geometry Analysis

The source PNG may show the iris at an angle.

Attempt to determine:

* circular vs elliptical iris appearance
* horizontal/vertical scale
* approximate viewing angle
* pupil eccentricity

If the source iris is elliptical because of perspective, allow the user to specify whether:

1. the ellipse is the intended graphic, or
2. it represents a circular iris viewed at an angle and should be normalized.

Provide a manual override.

---

# 7. Automatic Segmentation

The segmentation system should use multiple signals rather than relying on one simplistic threshold.

Potential signals:

* color
* saturation
* brightness
* local contrast
* radial symmetry
* circular/elliptical geometry
* edge detection
* connected components
* texture
* center-relative distance

Do not hard-code assumptions such as:

> "dark pixels = pupil"

because many eye designs will contain dark iris regions.

Likewise:

> "white pixels = sclera"

is insufficient.

---

# 8. Confidence Map

Every automatically detected component should have an associated confidence value.

Example:

```json
{
    "iris": 0.94,
    "pupil": 0.98,
    "sclera": 0.89,
    "highlight": 0.61,
    "eyelid": 0.76
}
```

The UI should visually indicate uncertain detections.

The application should NOT silently make aggressive guesses when confidence is low.

---

# 9. Manual Correction Interface

Provide an editing mode.

The user must be able to manually adjust:

* iris center
* iris boundary
* pupil boundary
* pupil center
* eye boundary
* eyelid mask
* highlight mask
* foreground mask

Useful tools:

* brush
* eraser
* polygon selection
* ellipse selection
* flood fill
* threshold adjustment
* feather
* expand mask
* shrink mask
* undo/redo

Include zoom.

Include a high-contrast mask display.

Include separate visibility toggles for every generated layer.

---

# 10. Layer Preview

Display:

1. Original PNG
2. Detected boundaries
3. Sclera
4. Iris
5. Pupil
6. Highlights
7. Eyelids
8. Foreground
9. Composite result

Allow layers to be toggled individually.

Also provide:

**"Show reconstructed image"**

The reconstructed image should approximate the original source image.

This is an important validation step.

---

# 11. Renderer Preview

Implement a preview that behaves like the actual Uncanny Eyes renderer.

The user should be able to move:

* gaze left/right
* gaze up/down
* pupil dilation
* eyelid position where supported

The preview should demonstrate that the generated layers actually work as independently movable components.

Provide controls such as:

```text
Gaze X     [---------●---------]
Gaze Y     [---------●---------]

Pupil      [------●------------]

Blink      [------------------●]
```

Also provide an automatic gaze animation.

---

# 12. Preserve Original Appearance

The most important design requirement is:

**The generated eye should look like the supplied PNG, not like a generic Uncanny Eyes eye.**

Do not unnecessarily recolor or redraw the eye.

Preserve:

* iris colors
* iris texture
* pupil appearance
* sclera shading
* veins
* shadows
* highlights
* unusual colors
* stylized artwork
* non-human characteristics

The generator should primarily **separate and prepare** the artwork rather than redesign it.

---

# 13. Resolution Handling

The application must determine the target rendering resolution based on the actual Uncanny Eyes display configuration.

Do not blindly resize the source image to an arbitrary size.

Provide:

* source resolution
* target resolution
* preview scaling
* optional supersampling
* optional downsampling

The output must ultimately be compatible with the graphics format expected by the target Uncanny Eyes implementation.

---

# 14. Color / Palette Processing

Determine what color format the target hardware/software requires.

Provide a conversion stage that can:

* convert RGB/RGBA artwork
* reduce colors when required
* generate an appropriate palette
* preserve important iris gradients
* use dithering when beneficial

Do NOT perform aggressive palette reduction before the user can inspect the result.

Show:

**Original**

vs.

**Target-format preview**

---

# 15. Transparency

Correctly handle alpha.

Every generated layer should have a well-defined transparency model.

Avoid black backgrounds being accidentally interpreted as actual eye artwork.

Validate:

* transparent pixels
* opaque pixels
* partially transparent pixels
* mask edges

---

# 16. Uncanny Eyes Integration

Study the current Adafruit Uncanny Eyes repository before implementing the exporter.

Specifically inspect:

* `uncannyEyes`
* `graphics`
* `convert`
* existing eye designs
* graphics header format
* rendering code
* coordinate conventions
* bitmap formats
* display dimensions
* animation assumptions

The existing repository explicitly contains a `convert` directory for generating graphics header files and a graphics collection used by the Arduino sketch. Use those existing formats rather than inventing an incompatible replacement.

Do NOT modify the core renderer unless absolutely necessary.

The preferred architecture is:

**generator → produces assets compatible with existing renderer**

rather than:

**generator → requires a replacement renderer**

---

# 17. Export Structure

Generate a self-contained output directory.

Example:

```text
generated_eye/
│
├── eye.json
│
├── source/
│   └── original.png
│
├── layers/
│   ├── sclera.png
│   ├── sclera_mask.png
│   ├── iris.png
│   ├── iris_mask.png
│   ├── pupil.png
│   ├── pupil_mask.png
│   ├── highlight.png
│   ├── highlight_mask.png
│   ├── eyelid_mask.png
│   └── foreground.png
│
├── preview/
│   ├── reconstructed.png
│   └── animated.gif
│
└── uncanny_eyes/
    ├── generated_eye.h
    └── [any required supporting files]
```

The exact final structure should follow the actual Uncanny Eyes source requirements discovered during implementation.

---

# 18. Generated Header

The exporter should ultimately generate the same type of graphics/header data expected by the Uncanny Eyes project.

The user should be able to copy the generated output into an Uncanny Eyes project and select/use the new eye design with minimal manual editing.

If the existing project requires a specific naming convention or array structure, follow that convention exactly.

---

# 19. Validation

Before declaring an export successful, automatically test:

### Asset validation

* all expected files exist
* dimensions are valid
* color format is valid
* alpha is valid
* no corrupted images

### Geometry validation

* pupil lies within iris
* iris lies within usable eye region
* masks have valid dimensions
* gaze movement does not expose impossible regions

### Renderer validation

Render several test positions:

```text
center
left
right
up
down
upper-left
upper-right
lower-left
lower-right
```

Check that the generated eye remains visually coherent.

---

# 20. Test Images

Create a test suite containing:

1. realistic human eye
2. cartoon eye
3. anime-style eye
4. robotic eye
5. reptile eye
6. fantasy eye
7. very dark iris
8. very light iris
9. slit pupil
10. asymmetric eye

The generator should not be designed around one particular sample image.

---

# 21. Architecture

Prefer a modular architecture.

Suggested components:

```text
Input
  ↓
Image Loader
  ↓
Preprocessor
  ↓
Eye Detector
  ↓
Segmentation Engine
  ├── Sclera Detector
  ├── Iris Detector
  ├── Pupil Detector
  ├── Highlight Detector
  └── Eyelid/Foreground Detector
  ↓
Geometry Solver
  ↓
Mask Refinement
  ↓
Layer Generator
  ↓
Renderer Preview
  ↓
Uncanny Eyes Converter
  ↓
Exporter
```

Do not combine everything into one giant function.

---

# 22. Keep Intermediate Data

Do not make the generated PNGs the only representation.

Keep a project file containing:

* source image reference
* detected geometry
* masks
* segmentation parameters
* corrections
* palette settings
* target display configuration
* export configuration

This allows the user to reopen a project and modify the segmentation without starting over.

Suggested:

```text
eye_project.json
```

or a project directory containing the source and editable masks.

---

# 23. CLI Support

In addition to the GUI, provide a command-line interface.

Example:

```bash
eye-generator input.png --output my_eye/
```

Optional:

```bash
eye-generator input.png \
    --output my_eye/ \
    --target uncanny-eyes \
    --display 160x128
```

And:

```bash
eye-generator my_eye.eyeproject --export
```

The GUI and CLI should use the same underlying processing library.

---

# 24. Batch Mode

Support processing multiple eyes later.

Example:

```bash
eye-generator ./eyes/*.png --output ./generated/
```

Each source image should become its own eye package.

---

# 25. Error Handling

If the application cannot confidently identify a component, do NOT silently produce bad artwork.

Instead report something such as:

```text
Iris detected: HIGH confidence
Pupil detected: HIGH confidence
Sclera detected: MEDIUM confidence
Eyelid detected: LOW confidence

Manual correction recommended for eyelid mask.
```

The export should still be possible if the user explicitly accepts the result.

---

# 26. Performance

The application should be usable on a CPU-only Linux development machine.

Do NOT make a discrete GPU mandatory.

Prefer:

* OpenCV
* NumPy
* Pillow
* standard image-processing algorithms
* optional ML components rather than mandatory GPU inference

If an ML model is used, it should have a CPU-compatible fallback.

Do not introduce a huge model simply to perform basic segmentation.

---

# 27. Technology Recommendation

Build the processing engine independently from the GUI.

Suggested:

```text
Python
├── Pillow
├── NumPy
├── OpenCV
└── optional ML segmentation backend
```

Possible GUI:

* PySide6

Alternative:

* browser-based frontend with a local Python backend

Choose the simplest architecture that provides accurate mask editing and preview.

---

# 28. Important Design Principle

There are actually TWO different problems:

### Problem A — Understand the artwork

Determine:

```text
What pixels belong to what eye component?
```

### Problem B — Make those components behave dynamically

Determine:

```text
How can those components be transformed/moved while still looking like an eye?
```

Do not treat these as the same problem.

A perfect segmentation can still produce a bad animated eye if the geometry and occlusion model are wrong.

---

# 29. Stretch Goal — AI-Assisted Segmentation

If practical, add an optional AI-assisted segmentation backend.

The AI should NOT directly generate a replacement eye.

Instead use AI to propose masks:

```text
AI segmentation
      ↓
candidate masks
      ↓
traditional image processing refinement
      ↓
user correction
      ↓
final masks
```

The original PNG remains the source artwork.

---

# 30. Stretch Goal — "One Click" Mode

Eventually support:

```text
DROP PNG HERE
       ↓
ANALYZE
       ↓
GENERATE
       ↓
PREVIEW
       ↓
EXPORT
```

For a well-formed source image, the user should ideally need no manual editing.

---

# 31. Stretch Goal — Renderer Round Trip

The strongest version of the application should be able to:

1. Import PNG.
2. Generate layers.
3. Generate Uncanny Eyes data.
4. Run the generated data through the actual renderer.
5. Capture the rendered result.
6. Compare it against the original.
7. Display the difference.

Provide a visual difference view:

```text
SOURCE       GENERATED       DIFFERENCE
[image]      [image]         [heatmap]
```

This would make it possible to automatically detect problems such as:

* incorrect iris size
* wrong center
* bad masking
* color loss
* missing highlights
* incorrect clipping

---

# 32. Development Phases

## Phase 1 — Repository Analysis

Before writing the generator:

* inspect current Uncanny Eyes source
* inspect `convert`
* inspect existing graphics
* determine exact asset format
* determine exact renderer expectations
* document coordinate systems
* identify what is actually required to create a new eye

Deliverable:

`UNCANNY_EYES_FORMAT.md`

---

## Phase 2 — Image Import

Implement:

* PNG loading
* alpha handling
* normalization
* preview
* image cropping
* project creation

Deliverable:

Working image importer.

---

## Phase 3 — Automatic Segmentation

Implement:

* eye boundary
* sclera
* iris
* pupil
* highlight
* eyelid/foreground masks

Deliverable:

Automatically segmented eye.

---

## Phase 4 — Manual Mask Editor

Implement:

* brush
* eraser
* polygon
* ellipse
* threshold
* feather
* expand/shrink
* undo/redo
* layer visibility

Deliverable:

User can correct segmentation.

---

## Phase 5 — Geometry Engine

Implement:

* eye center
* iris center
* pupil center
* iris dimensions
* pupil dimensions
* movement limits
* occlusion geometry

Deliverable:

Eye can be dynamically repositioned.

---

## Phase 6 — Renderer Preview

Implement:

* gaze controls
* pupil controls
* animation
* eyelid controls if supported

Deliverable:

Interactive animated eye preview.

---

## Phase 7 — Uncanny Eyes Export

Implement exact compatibility with the current Adafruit project.

Deliver:

```text
generated_eye.h
```

plus all required supporting data.

Deliverable:

Generated eye works in the actual Uncanny Eyes project.

---

## Phase 8 — Validation

Create automated tests for:

* dimensions
* masks
* color conversion
* geometry
* export
* renderer compatibility

Deliverable:

Repeatable test suite.

---

## Phase 9 — Polish

Add:

* drag-and-drop
* project save/load
* batch mode
* confidence visualization
* source/generated comparison
* export presets
* documentation

---

# 33. Definition of Done

The project is considered successful when I can do this:

```text
1. Start the application.

2. Drop in:
      dragon_eye.png

3. The application automatically identifies:
      sclera
      iris
      pupil
      highlights
      eyelids/occlusion

4. I inspect the generated masks.

5. I make a few corrections if necessary.

6. I press:
      EXPORT TO UNCANNY EYES

7. The application creates the required graphics/header files.

8. I put those files into the Adafruit Uncanny Eyes project.

9. The eye renders correctly.

10. The iris/pupil can move dynamically while the eyelid,
    highlights, and other stationary artwork remain visually correct.
```

The ultimate goal is:

**PNG → automatically separated eye → editable layers → Uncanny Eyes-compatible assets → animated eye**

without requiring the user to manually construct the graphics from scratch.

