# Build Plan — normalized pupil-to-limbus ratio model

This plan is based on the live implementation in the current source tree and is intended to be implementation-ready without changing the renderer architecture beyond the smallest required correction.

## 1. Audit conclusion: what the current renderer is actually doing

The active renderer does not use a distinct geometric pupil-diameter layer. It uses a polar iris strip and thresholding model:

- `src/eye.cpp` computes `irisThreshold` from `irisScale`.
- `src/eye.cpp` then samples the polar iris strip and falls back to the sclera when the sampled distance exceeds that threshold.
- The visible “pupil” is therefore the region where the iris sampling stops and sclera becomes visible.
- The actual geometric boundary is the radial sampling threshold, not a separate `pupilRadius` object in the render path.

The exact live calculation is in `pupilGeometry()` and `EyeRenderer::render()`:

```cpp
geometry.irisRadius = static_cast<double>(irisHeight) / 2.0;
geometry.irisThreshold = (128 * (1023 - std::clamp(irisScale, 0, 1023)) + 512) / 1024;
const double raw = (127.0 - geometry.irisThreshold) / 128.0 * geometry.irisRadius;
geometry.radius = std::max(0.0, std::min(raw, geometry.irisRadius));
```

and in the renderer:

```cpp
const int irisThreshold = (128 * (1023 - input.irisScale) + 512) / 1024;
const std::uint32_t irisScaleFixed =
    static_cast<std::uint32_t>(artwork_.irisMapHeight) * 65536U /
    static_cast<std::uint32_t>(std::max(1, irisThreshold));
```

This means the current implementation is mathematically using the thresholded radial distance as the effective pupil boundary. It is not applying a calibrated pupil-to-limbus ratio independent of the iris texture.

### Current meaning of `irisScale`

The comment in `src/eye.h` says higher `irisScale` is “more constricted,” but the actual formula does the opposite: as `irisScale` grows, `irisThreshold` shrinks, so the iris samples over a smaller disc and the sclera reveals a larger pupil. In other words, the code is implementing:

- larger `irisScale` => lower radial threshold => larger visible pupil
- smaller `irisScale` => larger threshold => smaller visible pupil

The implementation is therefore effectively using the threshold as the pupil boundary, but it is doing so in a way that is not explicitly tied to an eye-specific limbus diameter and not normalized as a ratio model.

## 2. Current geometry and coordinate system

### 2.1 Normalized canvas

The normalized input asset pipeline in `create/normalize_eye.py` explicitly re-centres the source eye so the pupil center lands at the frame centre. The script’s `shift_to_centre()` routine translates the source image so the measured pupil centre is brought to the frame midpoint. This is the source-side origin of the normalized 200x200 design.

The expected normalized eye canvas is a 200×200 plate with the pupil centre at:

```text
(100, 100)
```

The renderer itself uses a fixed polar disc centered on the midpoint of the iris texture:

```cpp
const double centre = artwork_.irisHeight / 2.0;
const double dx = irisX + 0.5 - centre;
const double dy = irisY + 0.5 - centre;
const double rho2 = dx * dx + dy * dy;
```

This center is not a runtime calibration variable; it is the origin of the generated source artwork and the polar sampling circle.

### 2.2 Current pupil center

The current runtime has the pupil center effectively fixed at the center of the iris disc. In generated eye assets, the normalized source is re-centered so the measured pupil sits at the frame mid-point. That makes the operational center:

```text
pupilCenter = (100, 100)
```

This is the correct center that should be preserved in the ratio-based implementation.

### 2.3 Current limbus geometry source

The source-side geometry is measured in `create/normalize_eye.py`:

- `find_pupil()` finds the dark pupil blob and its center.
- `find_pupil_radius()` measures the pupil edge from a radial luminance profile.
- `find_limbus()` measures the limbus boundary from the outer iris profile.
- `unroll_iris()` creates the radial iris strip.

The important part is that the limbus is a measured source geometry, not a runtime object. The runtime renderer only sees the generated iris strip and the final `irisHeight` / `irisRadius` in the artwork metadata. This is enough to derive an eye-specific disc radius but not enough to preserve the conceptual separation between eye geometry and normalized dilation unless we add an explicit calibration layer.

### 2.4 Existing iris sampling boundary

The live radial boundary is determined by `irisThreshold` and the polar lookup:

```cpp
const bool withinIris = irisThreshold > 0 && rho2 <= centre * centre;
const std::uint16_t polar = artwork_.polar[irisY * artwork_.irisWidth + irisX];
const int distance = polar & 0x7f;
```

If `distance < irisThreshold`, the renderer samples iris texture; otherwise it draws sclera. The exact visible pupil is therefore produced by the iris/sclera cutoff, and the cutoff is controlled by the threshold derived from `irisScale`.

## 3. Defect in the current model

The current code collapses the geometry into a single scale variable and therefore makes the pupil boundary an implicit consequence of the thresholded sampling. This is exactly why the project currently has ambiguity: the system appears to have a pupil size variable, but no explicit calibration object with these separate concepts:

- normalized dilation
- limbus diameter
- pupil-to-limbus ratio
- pupil radius
- fixed center

The fix is not to redesign the whole renderer; it is to introduce the normalized ratio model at the boundary where the current code derives the pupil radius from `irisScale` and thresholding.

## 4. Required normalized ratio model

This is the required implementation model for the renderer:

```text
D = clamp(dilation, 0.0, 1.0)

pupilRatio = Rmin + D × (Rmax - Rmin)

pupilDiameter = limbusDiameter × pupilRatio

pupilRadius = pupilDiameter / 2
```

with:

```text
pupilCenter = (100, 100)
```

and the calibration relationship:

```text
normalized dilation
    ↓
pupil/limbus ratio
    ↓
eye-specific limbus diameter
    ↓
pupil diameter
    ↓
pupil radius
    ↓
existing polar iris sampling boundary
```

The important rule is that the source artwork and sensor input remain separate:

- LDR / other input source feeds normalized dilation
- eye calibration feeds limbus geometry and `Rmin` / `Rmax`
- pupil radius is derived from the combination
- renderer keeps using the existing polar sampling model

## 5. Proposed calibration interface

Add a small eye calibration structure that is read by the renderer or the animator before the render input is built.

```cpp
struct EyeCalibration {
    float limbusDiameter = 100.0F;  // eye-specific diameter in normalized canvas units
    float pupilRatioMin = 0.12F;   // Rmin for D = 0.0
    float pupilRatioMax = 0.42F;   // Rmax for D = 1.0
    float pupilCenterX = 100.0F;
    float pupilCenterY = 100.0F;
};
```

This is the minimum abstraction that preserves the normalized geometry without redesigning the existing shaders or generated artwork. It does not encode the sensor; it encodes the eye.

## 6. Integration point into the existing renderer

The correct implementation boundary is where the current code converts `irisScale` into the thresholded radius. The smallest change is to preserve the renderer’s polar sampling path and replace the ad hoc threshold computation with a calibrated pupil radius.

The current code path is:

```cpp
pupilGeometry(...)
  -> irisThreshold
  -> radius = clamp((127 - irisThreshold)/128 * irisRadius, 0, irisRadius)
  -> draw iris / sclera cutoff
```

The new implementation should instead derive a calibrated `pupilRadius` before the render threshold is applied:

```cpp
float D = clamp(dilation, 0.0f, 1.0f);
float pupilRatio = Rmin + D * (Rmax - Rmin);
float pupilDiameter = limbusDiameter * pupilRatio;
float pupilRadius = pupilDiameter / 2.0f;
```

Then map that pupil radius back into the same threshold logic the renderer already understands:

```cpp
// keep the current polar path intact
irisThreshold = clamp(irisRadius - pupilRadius, 0.0, irisRadius);
```

or an equivalent, renderer-accurate mapping that preserves the same interpretation: the pupil occupies the center disc, and the iris occupies the ring outside it. The important part is not the exact threshold formula name, but the fact that the boundary must now be derived from calibrated ratio geometry rather than from a raw `irisScale` magic value.

This is the smallest possible implementation because the existing renderer already knows how to sample the polar disc and how to reveal the sclera outside the pupil.

## 7. Why the current renderer allows incorrect pupil behavior

The current implementation allows pupil expansion to appear “wrong” because it has no explicit bound on the pupil/limbus ratio. Instead it uses a generic threshold derived from `irisScale`, which:

1. mixes the sensor/dilation concept with artwork geometry,
2. does not represent a calibrated limbus diameter,
3. has no eye-specific minimum and maximum ratio constraints,
4. can permit the visible pupil to expand to the entire disc if the threshold is driven incorrectly,
5. relies on the art and the threshold together to hide that fact.

The offending state is the exact quantity that must be constrained:

```text
pupilRadius = calibrated from pupilRatio × limbusDiameter / 2
```

not an arbitrary threshold value.

## 8. Files that must change

Implementation will be confined to the logic layer and the docs, not to the artwork pipeline itself.

### Must change

- `src/eye.h` — extend the runtime contract with an eye calibration object and/or a render-time pupil-radius input.
- `src/eye.cpp` — replace the raw `irisScale`-based threshold logic with a calibrated pupil-radius boundary derived from `pupilRatio`.
- `src/main.cpp` or the input wiring layer — preserve the LDR / sensor feed as normalized dilation only.
- `src/PupilResponseModel.*` — keep the physiological response model as the normalized dilation signal generator.
- `BUILDPLAN.md` — update with the final implementation-ready geometry and validation plan.
- `TODO.md` — update with the execution sequence and test targets.

### Must not change

- `create/normalize_eye.py` should not be redesignated as part of the runtime fix; it remains the asset-generation measurement tool.
- The full 200×200 normalized eye source generation is already the correct source coordinate system and should remain stable.
- The polar renderer data and existing artwork generation path should remain intact.
- The LDR input path should not get merged into eye calibration.
- The generated art tables should not be rewritten as part of this geometry fix unless the planned calibration is explicitly data-driven.

## 9. Validation strategy

The test strategy must prove that the normalized ratio model matches the geometry independently of the visual artwork.

### Unit tests

1. Minimum dilation
   ```text
   D = 0.0
   pupilRatio = Rmin
   ```

2. Midpoint dilation
   ```text
   D = 0.5
   pupilRatio = Rmin + 0.5 × (Rmax - Rmin)
   ```

3. Maximum dilation
   ```text
   D = 1.0
   pupilRatio = Rmax
   ```

4. Clamp behavior
   ```text
   D < 0 -> 0
   D > 1 -> 1
   ```

5. Cross-eye proportionality
   ```text
   Eye A: L = 100
   Eye B: L = 130
   At same D: PupilA / L_A == PupilB / L_B
   ```

6. Center invariance
   ```text
   pupilCenter = (100,100) for every D
   ```

7. Boundary invariance
   ```text
   Rmin <= pupilRatio <= Rmax
   ```

### Visual/manual validation

- render at minimum dilation, midpoint, and maximum dilation
- verify the pupil remains centered at the mid-point of the normalized eye
- verify the pupil never expands beyond the calibrated `Rmax`
- verify eye assets with different limbus diameters preserve the same ratio relationship
- verify the sclera reveal still comes from the existing polar cutoff path and not from a separate black circle

## 10. Implementation order

1. Add eye calibration object with `limbusDiameter`, `Rmin`, `Rmax`, and center.
2. Add normalized dilation propagation from the source input to the render input.
3. Add ratio interpolation: `Rmin + D*(Rmax-Rmin)`.
4. Compute `pupilDiameter = limbusDiameter * pupilRatio` and `pupilRadius = pupilDiameter/2`.
5. Replace the raw threshold logic with a calibrated boundary derived from `pupilRadius`.
6. Preserve the existing polar texture sampling and sclera fallback.
7. Validate that the pupil remains centered and bounded for all test values.
8. Run cross-eye and asset compatibility checks.

## 11. Final implementation model

The final target is unambiguous:

```text
200×200 normalized canvas
    |
    +-- pupil center = (100,100)
    |
    +-- eye-specific limbus geometry
    |
    +-- normalized dilation 0.0 → 1.0
    |
    +-- Rmin → Rmax interpolation
    |
    +-- pupil-to-limbus ratio
    |
    +-- eye-specific pupil diameter
    |
    +-- pupil radius
    |
    +-- existing polar iris sampling boundary
    |
    +-- existing sclera reveal
```

The thing to standardize is not arbitrary pixel size. It is the pupil-to-limbus ratio as a function of normalized dilation, with the eye’s limbus providing the absolute scale and the 200×200 normalized canvas providing the coordinate system.
